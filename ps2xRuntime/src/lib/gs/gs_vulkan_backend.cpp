#include "runtime/gs/gs_vulkan_backend.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>

#if defined(__ANDROID__)
namespace
{
    bool vkOk(VkResult result, const char *operation)
    {
        if (result == VK_SUCCESS)
            return true;
        std::cerr << "[GSVulkanBackend] " << operation << " failed: " << static_cast<int>(result) << std::endl;
        return false;
    }

    struct BufferResource
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };
}

uint32_t GSVulkanBackend::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((typeBits & (1u << i)) != 0u &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

bool GSVulkanBackend::InitializeVulkan()
{
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "PS2Recomp Android GS";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "PS2Recomp";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &appInfo;
    if (!vkOk(vkCreateInstance(&instanceInfo, nullptr, &m_instance), "vkCreateInstance"))
        return false;

    uint32_t physicalCount = 0;
    if (!vkOk(vkEnumeratePhysicalDevices(m_instance, &physicalCount, nullptr), "vkEnumeratePhysicalDevices(count)") || physicalCount == 0)
        return false;

    std::vector<VkPhysicalDevice> devices(physicalCount);
    if (!vkOk(vkEnumeratePhysicalDevices(m_instance, &physicalCount, devices.data()), "vkEnumeratePhysicalDevices"))
        return false;

    for (VkPhysicalDevice candidate : devices)
    {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u)
            {
                m_physicalDevice = candidate;
                m_queueFamily = i;
                break;
            }
        }
        if (m_physicalDevice != VK_NULL_HANDLE)
            break;
    }
    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        std::cerr << "[GSVulkanBackend] No Vulkan graphics queue found" << std::endl;
        return false;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
    std::cout << "[GSVulkanBackend] Vulkan GPU: " << properties.deviceName
              << " API " << VK_VERSION_MAJOR(properties.apiVersion) << "."
              << VK_VERSION_MINOR(properties.apiVersion) << std::endl;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (!vkOk(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "vkCreateDevice"))
        return false;
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    if (!vkOk(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool"))
        return false;

    return true;
}

bool GSVulkanBackend::RoundTripFrame(PresentationFrame &frame)
{
    if (!m_vulkanReady || !frame || frame.width > 8192u || frame.height > 8192u)
        return false;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(frame.width) * frame.height * 4u;
    if (byteCount == 0 || byteCount > std::numeric_limits<uint32_t>::max() || frame.pixels.size() < byteCount)
        return false;

    auto createBuffer = [&](VkBufferUsageFlags usage, BufferResource &out) -> bool
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = byteCount;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!vkOk(vkCreateBuffer(m_device, &info, nullptr, &out.buffer), "vkCreateBuffer"))
            return false;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(m_device, out.buffer, &requirements);
        const uint32_t memoryType = FindMemoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memoryType == UINT32_MAX)
            return false;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (!vkOk(vkAllocateMemory(m_device, &allocation, nullptr, &out.memory), "vkAllocateMemory(buffer)"))
            return false;
        if (!vkOk(vkBindBufferMemory(m_device, out.buffer, out.memory, 0), "vkBindBufferMemory"))
            return false;
        out.size = byteCount;
        return true;
    };

    BufferResource upload{}, readback{};
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool success = false;

    if (!createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, upload) ||
        !createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback))
        goto cleanup;

    {
        void *mapped = nullptr;
        if (!vkOk(vkMapMemory(m_device, upload.memory, 0, byteCount, 0, &mapped), "vkMapMemory(upload)"))
            goto cleanup;
        std::memcpy(mapped, frame.pixels.data(), static_cast<size_t>(byteCount));
        vkUnmapMemory(m_device, upload.memory);
    }

    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent = {frame.width, frame.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!vkOk(vkCreateImage(m_device, &info, nullptr, &image), "vkCreateImage"))
            goto cleanup;
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(m_device, image, &requirements);
        const uint32_t memoryType = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (memoryType == UINT32_MAX)
            goto cleanup;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        if (!vkOk(vkAllocateMemory(m_device, &allocation, nullptr, &imageMemory), "vkAllocateMemory(image)") ||
            !vkOk(vkBindImageMemory(m_device, image, imageMemory, 0), "vkBindImageMemory"))
            goto cleanup;
    }

    {
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = m_commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (!vkOk(vkAllocateCommandBuffers(m_device, &alloc, &command), "vkAllocateCommandBuffers"))
            goto cleanup;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!vkOk(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer"))
            goto cleanup;

        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = image;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {frame.width, frame.height, 1};
        vkCmdCopyBufferToImage(command, upload.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = image;
        toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toRead);
        vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
        if (!vkOk(vkEndCommandBuffer(command), "vkEndCommandBuffer"))
            goto cleanup;

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!vkOk(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "vkCreateFence"))
            goto cleanup;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        if (!vkOk(vkQueueSubmit(m_queue, 1, &submit, fence), "vkQueueSubmit") ||
            !vkOk(vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
            goto cleanup;
    }

    {
        void *mapped = nullptr;
        if (!vkOk(vkMapMemory(m_device, readback.memory, 0, byteCount, 0, &mapped), "vkMapMemory(readback)"))
            goto cleanup;
        std::memcpy(frame.pixels.data(), mapped, static_cast<size_t>(byteCount));
        vkUnmapMemory(m_device, readback.memory);
    }
    success = true;

cleanup:
    if (m_device != VK_NULL_HANDLE)
    {
        if (fence != VK_NULL_HANDLE) vkDestroyFence(m_device, fence, nullptr);
        if (command != VK_NULL_HANDLE) vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
        if (image != VK_NULL_HANDLE) vkDestroyImage(m_device, image, nullptr);
        if (imageMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, imageMemory, nullptr);
        if (upload.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, upload.buffer, nullptr);
        if (upload.memory != VK_NULL_HANDLE) vkFreeMemory(m_device, upload.memory, nullptr);
        if (readback.buffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, readback.buffer, nullptr);
        if (readback.memory != VK_NULL_HANDLE) vkFreeMemory(m_device, readback.memory, nullptr);
    }
    return success;
}
#endif

GSVulkanBackend::~GSVulkanBackend()
{
#if defined(__ANDROID__)
    ShutdownVulkan();
#endif
}

void GSVulkanBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    m_cpu.Initialize(vram, vramSize);
#if defined(__ANDROID__)
    // GS rasterization remains on the existing CPU backend for this integration
    // stage. AndroidVulkanPresenter owns the NativeActivity surface and presents
    // these completed GS frames through a real Vulkan swapchain.
    m_vulkanReady = false;
    ShutdownVulkan();
#else
    m_vulkanReady = false;
#endif
}

void GSVulkanBackend::ShutdownVulkan()
{
#if defined(__ANDROID__)
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
        if (m_commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr);
    m_commandPool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
#endif
    m_vulkanReady = false;
}

void GSVulkanBackend::Reset() { m_cpu.Reset(); }
void GSVulkanBackend::Submit(const GSPrimitiveBatch &batch) { m_cpu.Submit(batch); }
void GSVulkanBackend::LoadClut(const GSTex0Reg &a, const GSTexClutReg &b) { m_cpu.LoadClut(a, b); }
void GSVulkanBackend::BeginTransfer(const GSTransferCommand &command) { m_cpu.BeginTransfer(command); }
void GSVulkanBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes) { m_cpu.UploadImage(data, sizeBytes); }
void GSVulkanBackend::Flush() { m_cpu.Flush(); }
void GSVulkanBackend::TextureFlush() { m_cpu.TextureFlush(); }
void GSVulkanBackend::Sync(GSSyncReason reason) { m_cpu.Sync(reason); }

PresentationFrame GSVulkanBackend::Present(const GSPresentationRequest &request)
{
    // The CPU GS produces the guest framebuffer. The Android runtime consumes
    // that framebuffer and submits it to the Vulkan swapchain for presentation.
    return m_cpu.Present(request);
}

bool GSVulkanBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba) { return m_cpu.ClearFramebuffer(context, rgba); }
uint32_t GSVulkanBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) { return m_cpu.ConsumeLocalToHostBytes(dst, maxBytes); }
uint32_t GSVulkanBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const { return m_cpu.ReadVram(psm, base, bw, x, y); }
void GSVulkanBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) { m_cpu.WriteVram(psm, base, bw, x, y, value); }
void GSVulkanBackend::SnapshotVram(std::vector<uint8_t> &out) const { m_cpu.SnapshotVram(out); }
GSTransferSnapshot GSVulkanBackend::GetTransferSnapshot() const { return m_cpu.GetTransferSnapshot(); }
