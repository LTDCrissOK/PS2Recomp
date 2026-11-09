#include "runtime/gs/android_vulkan_presenter.h"
#if defined(__ANDROID__)
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
bool checkVk(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) return true;
    std::cerr << "[AndroidVulkanPresenter] " << operation << " failed: " << static_cast<int>(result) << std::endl;
    return false;
}
}

AndroidVulkanPresenter::~AndroidVulkanPresenter() { Shutdown(); }

uint32_t AndroidVulkanPresenter::FindMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
{
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(m_gpu, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return UINT32_MAX;
}

bool AndroidVulkanPresenter::Initialize(ANativeWindow *window)
{
    if (!window) return false;
    Shutdown();
    m_window = window;
    ANativeWindow_acquire(m_window);

    const char *instanceExtensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "PS2Recomp Manhunt 2";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    if (!checkVk(vkCreateInstance(&instanceInfo, nullptr, &m_instance), "vkCreateInstance")) { Shutdown(); return false; }

    VkAndroidSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.window = m_window;
    if (!checkVk(vkCreateAndroidSurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface), "vkCreateAndroidSurfaceKHR")) { Shutdown(); return false; }

    uint32_t gpuCount = 0;
    if (!checkVk(vkEnumeratePhysicalDevices(m_instance, &gpuCount, nullptr), "vkEnumeratePhysicalDevices") || gpuCount == 0) { Shutdown(); return false; }
    std::vector<VkPhysicalDevice> gpus(gpuCount);
    if (!checkVk(vkEnumeratePhysicalDevices(m_instance, &gpuCount, gpus.data()), "enumerate GPUs")) { Shutdown(); return false; }

    for (VkPhysicalDevice candidate : gpus)
    {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_surface, &canPresent);
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent)
            {
                m_gpu = candidate;
                m_queueFamily = i;
                break;
            }
        }
        if (m_gpu != VK_NULL_HANDLE) break;
    }
    if (m_gpu == VK_NULL_HANDLE) { std::cerr << "[AndroidVulkanPresenter] No graphics+present queue" << std::endl; Shutdown(); return false; }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char *deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    if (!checkVk(vkCreateDevice(m_gpu, &deviceInfo, nullptr, &m_device), "vkCreateDevice")) { Shutdown(); return false; }
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

    if (!CreateSwapchain() || !CreateStagingBuffer()) { Shutdown(); return false; }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    if (!checkVk(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool")) { Shutdown(); return false; }
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = m_commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (!checkVk(vkAllocateCommandBuffers(m_device, &commandInfo, &m_command), "vkAllocateCommandBuffers")) { Shutdown(); return false; }

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!checkVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_acquired), "create acquire semaphore") ||
        !checkVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_complete), "create present semaphore")) { Shutdown(); return false; }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!checkVk(vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence), "vkCreateFence")) { Shutdown(); return false; }

    std::cout << "[AndroidVulkanPresenter] Vulkan presentation ready: " << m_extent.width << "x" << m_extent.height << std::endl;
    return true;
}

bool AndroidVulkanPresenter::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    if (!checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu, m_surface, &caps), "surface capabilities")) return false;
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
    {
        std::cerr << "[AndroidVulkanPresenter] Surface images do not support transfer-destination usage" << std::endl;
        return false;
    }
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &formatCount, nullptr);
    if (!formatCount) return false;
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &formatCount, formats.data());
    VkSurfaceFormatKHR selected = formats[0];
    for (const auto &format : formats)
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_B8G8R8A8_UNORM) { selected = format; break; }
    if (selected.format != VK_FORMAT_R8G8B8A8_UNORM && selected.format != VK_FORMAT_B8G8R8A8_UNORM) return false;
    m_format = selected.format;

    if (caps.currentExtent.width != UINT32_MAX) m_extent = caps.currentExtent;
    else
    {
        m_extent.width = std::clamp(static_cast<uint32_t>(ANativeWindow_getWidth(m_window)), caps.minImageExtent.width, caps.maxImageExtent.width);
        m_extent.height = std::clamp(static_cast<uint32_t>(ANativeWindow_getHeight(m_window)), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    uint32_t imageCount = std::max(caps.minImageCount + 1u, 2u);
    if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = m_surface;
    info.minImageCount = imageCount;
    info.imageFormat = m_format;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = m_extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    if (!checkVk(vkCreateSwapchainKHR(m_device, &info, nullptr, &m_swapchain), "vkCreateSwapchainKHR")) return false;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    return checkVk(vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data()), "get swapchain images");
}

bool AndroidVulkanPresenter::CreateStagingBuffer()
{
    m_stagingSize = static_cast<VkDeviceSize>(m_extent.width) * m_extent.height * 4u;
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = m_stagingSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_staging), "vkCreateBuffer")) return false;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_staging, &requirements);
    const uint32_t type = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (!checkVk(vkAllocateMemory(m_device, &allocation, nullptr, &m_stagingMemory), "vkAllocateMemory(staging)")) return false;
    return checkVk(vkBindBufferMemory(m_device, m_staging, m_stagingMemory, 0), "vkBindBufferMemory");
}

bool AndroidVulkanPresenter::Present(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height)
{
    if (!m_device || rgba.empty() || !width || !height || rgba.size() < static_cast<size_t>(width) * height * 4u) return false;
    if (!checkVk(vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX), "wait frame fence")) return false;
    uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_acquired, VK_NULL_HANDLE, &imageIndex);
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) { std::cerr << "[AndroidVulkanPresenter] acquire failed: " << acquired << std::endl; return false; }

    void *mapped = nullptr;
    if (!checkVk(vkMapMemory(m_device, m_stagingMemory, 0, m_stagingSize, 0, &mapped), "map staging")) return false;
    auto *dst = static_cast<uint8_t *>(mapped);
    std::memset(dst, 0, static_cast<size_t>(m_stagingSize));
    const float scale = std::min(static_cast<float>(m_extent.width) / static_cast<float>(width),
                                 static_cast<float>(m_extent.height) / static_cast<float>(height));
    const uint32_t drawWidth = std::max(1u, static_cast<uint32_t>(width * scale));
    const uint32_t drawHeight = std::max(1u, static_cast<uint32_t>(height * scale));
    const uint32_t offsetX = (m_extent.width - drawWidth) / 2u;
    const uint32_t offsetY = (m_extent.height - drawHeight) / 2u;
    for (uint32_t y = 0; y < drawHeight; ++y)
    {
        const uint32_t sy = std::min(height - 1u, static_cast<uint32_t>((static_cast<uint64_t>(y) * height) / drawHeight));
        for (uint32_t x = 0; x < drawWidth; ++x)
        {
            const uint32_t sx = std::min(width - 1u, static_cast<uint32_t>((static_cast<uint64_t>(x) * width) / drawWidth));
            const size_t source = (static_cast<size_t>(sy) * width + sx) * 4u;
            const size_t target = (static_cast<size_t>(offsetY + y) * m_extent.width + offsetX + x) * 4u;
            if (m_format == VK_FORMAT_B8G8R8A8_UNORM) { dst[target] = rgba[source + 2]; dst[target + 1] = rgba[source + 1]; dst[target + 2] = rgba[source]; }
            else { dst[target] = rgba[source]; dst[target + 1] = rgba[source + 1]; dst[target + 2] = rgba[source + 2]; }
            dst[target + 3] = rgba[source + 3];
        }
    }
    vkUnmapMemory(m_device, m_stagingMemory);

    vkResetFences(m_device, 1, &m_fence);
    vkResetCommandBuffer(m_command, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!checkVk(vkBeginCommandBuffer(m_command, &begin), "vkBeginCommandBuffer")) return false;

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = m_images[imageIndex];
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {m_extent.width, m_extent.height, 1};
    vkCmdCopyBufferToImage(m_command, m_staging, m_images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = m_images[imageIndex];
    toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);
    if (!checkVk(vkEndCommandBuffer(m_command), "vkEndCommandBuffer")) return false;

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_acquired;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_complete;
    if (!checkVk(vkQueueSubmit(m_queue, 1, &submit, m_fence), "vkQueueSubmit")) return false;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_complete;
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapchain;
    present.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(m_queue, &present);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) { std::cerr << "[AndroidVulkanPresenter] present failed: " << result << std::endl; return false; }
    return true;
}

void AndroidVulkanPresenter::Shutdown()
{
    if (m_device) vkDeviceWaitIdle(m_device);
    if (m_device)
    {
        if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
        if (m_acquired) vkDestroySemaphore(m_device, m_acquired, nullptr);
        if (m_complete) vkDestroySemaphore(m_device, m_complete, nullptr);
        if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        if (m_staging) vkDestroyBuffer(m_device, m_staging, nullptr);
        if (m_stagingMemory) vkFreeMemory(m_device, m_stagingMemory, nullptr);
        if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_instance && m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
    if (m_window) ANativeWindow_release(m_window);
    m_window = nullptr; m_instance = VK_NULL_HANDLE; m_surface = VK_NULL_HANDLE; m_gpu = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE; m_queue = VK_NULL_HANDLE; m_swapchain = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE; m_command = VK_NULL_HANDLE; m_staging = VK_NULL_HANDLE;
    m_stagingMemory = VK_NULL_HANDLE; m_fence = VK_NULL_HANDLE; m_acquired = VK_NULL_HANDLE; m_complete = VK_NULL_HANDLE;
    m_images.clear(); m_extent = {};
}
#endif
