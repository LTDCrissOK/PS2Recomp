#include <android/log.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PS2RecompVulkanTest", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PS2RecompVulkanTest", __VA_ARGS__)

static bool ok(VkResult r, const char* what) {
    if (r == VK_SUCCESS) return true;
    LOGE("%s failed: VkResult=%d", what, (int)r);
    return false;
}

struct VulkanTest {
    android_app* app{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice gpu{};
    VkDevice device{};
    uint32_t queueFamily{};
    VkQueue queue{};
    VkSwapchainKHR swapchain{};
    VkFormat format{};
    VkExtent2D extent{};
    VkRenderPass renderPass{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool{};
    std::vector<VkCommandBuffer> commands;
    VkSemaphore acquired{}, rendered{};
    VkFence fence{};
    uint32_t frame{};

    bool init() {
        const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
        VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        ai.pApplicationName = "PS2Recomp Vulkan Test";
        ai.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &ai;
        ici.enabledExtensionCount = 2;
        ici.ppEnabledExtensionNames = extensions;
        if (!ok(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance")) return false;

        VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        sci.window = app->window;
        if (!ok(vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface), "vkCreateAndroidSurfaceKHR")) return false;

        uint32_t count = 0;
        if (!ok(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices") || count == 0) {
            LOGE("No Vulkan physical devices found"); return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        if (!ok(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate physical devices")) return false;
        for (auto candidate : devices) {
            uint32_t qcount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qcount, nullptr);
            std::vector<VkQueueFamilyProperties> qprops(qcount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qcount, qprops.data());
            for (uint32_t i=0; i<qcount; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
                if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    gpu = candidate; queueFamily = i; break;
                }
            }
            if (gpu) break;
        }
        if (!gpu) { LOGE("No queue supports both graphics and Android presentation"); return false; }
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);
        LOGI("Vulkan GPU: %s | API %u.%u.%u", props.deviceName,
             VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = queueFamily; qci.queueCount = 1; qci.pQueuePriorities = &priority;
        const char* devExt[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExt;
        if (!ok(vkCreateDevice(gpu, &dci, nullptr, &device), "vkCreateDevice")) return false;
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        return createSwapchain();
    }

    bool createSwapchain() {
        VkSurfaceCapabilitiesKHR caps{};
        if (!ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps), "surface capabilities")) return false;
        uint32_t n=0; vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, nullptr);
        if (!n) { LOGE("Surface exposes no formats"); return false; }
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &n, formats.data());
        VkSurfaceFormatKHR chosen = formats[0];
        for (auto f : formats) if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { chosen=f; break; }
        format=chosen.format;
        if (caps.currentExtent.width != UINT32_MAX) extent=caps.currentExtent;
        else {
            extent.width=std::clamp((uint32_t)ANativeWindow_getWidth(app->window), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height=std::clamp((uint32_t)ANativeWindow_getHeight(app->window), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        uint32_t imageCount=std::max(caps.minImageCount+1, 2u);
        if (caps.maxImageCount && imageCount>caps.maxImageCount) imageCount=caps.maxImageCount;
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface=surface; ci.minImageCount=imageCount; ci.imageFormat=format; ci.imageColorSpace=chosen.colorSpace;
        ci.imageExtent=extent; ci.imageArrayLayers=1; ci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; ci.preTransform=caps.currentTransform;
        ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; ci.presentMode=VK_PRESENT_MODE_FIFO_KHR; ci.clipped=VK_TRUE;
        if (!ok(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain), "vkCreateSwapchainKHR")) return false;
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        images.resize(imageCount); vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

        VkAttachmentDescription attachment{};
        attachment.format=format; attachment.samples=VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount=1; sub.pColorAttachments=&ref;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount=1; rp.pAttachments=&attachment; rp.subpassCount=1; rp.pSubpasses=&sub;
        if (!ok(vkCreateRenderPass(device, &rp, nullptr, &renderPass), "vkCreateRenderPass")) return false;

        views.resize(images.size()); framebuffers.resize(images.size());
        for (size_t i=0;i<images.size();++i) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image=images[i]; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=format;
            vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            if (!ok(vkCreateImageView(device,&vi,nullptr,&views[i]),"vkCreateImageView")) return false;
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass=renderPass; fi.attachmentCount=1; fi.pAttachments=&views[i]; fi.width=extent.width; fi.height=extent.height; fi.layers=1;
            if (!ok(vkCreateFramebuffer(device,&fi,nullptr,&framebuffers[i]),"vkCreateFramebuffer")) return false;
        }
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex=queueFamily;
        if (!ok(vkCreateCommandPool(device,&pci,nullptr,&commandPool),"vkCreateCommandPool")) return false;
        commands.resize(images.size());
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cai.commandPool=commandPool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=(uint32_t)commands.size();
        if (!ok(vkAllocateCommandBuffers(device,&cai,commands.data()),"vkAllocateCommandBuffers")) return false;
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!ok(vkCreateSemaphore(device,&sem,nullptr,&acquired),"vkCreateSemaphore") ||
            !ok(vkCreateSemaphore(device,&sem,nullptr,&rendered),"vkCreateSemaphore")) return false;
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fci.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        if (!ok(vkCreateFence(device,&fci,nullptr,&fence),"vkCreateFence")) return false;
        LOGI("Vulkan swapchain ready: %ux%u, %zu images",extent.width,extent.height,images.size());
        return true;
    }

    void draw() {
        if (!device || !swapchain) return;
        vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX);
        uint32_t index=0;
        VkResult ar=vkAcquireNextImageKHR(device,swapchain,UINT64_MAX,acquired,VK_NULL_HANDLE,&index);
        if (ar==VK_ERROR_OUT_OF_DATE_KHR || ar==VK_SUBOPTIMAL_KHR) { LOGE("Swapchain needs recreation after surface change"); return; }
        if (ar!=VK_SUCCESS) { LOGE("vkAcquireNextImageKHR: %d",ar); return; }
        vkResetFences(device,1,&fence);
        vkResetCommandBuffer(commands[index],0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(commands[index],&bi);
        float t=(float)(frame++ % 600)/600.0f;
        VkClearValue clear{}; clear.color={{0.08f+0.35f*t,0.12f,0.35f-0.25f*t,1.0f}};
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass=renderPass; rbi.framebuffer=framebuffers[index]; rbi.renderArea={{0,0},extent};
        rbi.clearValueCount=1; rbi.pClearValues=&clear;
        vkCmdBeginRenderPass(commands[index],&rbi,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(commands[index]);
        vkEndCommandBuffer(commands[index]);
        VkPipelineStageFlags stage=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount=1; si.pWaitSemaphores=&acquired; si.pWaitDstStageMask=&stage;
        si.commandBufferCount=1; si.pCommandBuffers=&commands[index];
        si.signalSemaphoreCount=1; si.pSignalSemaphores=&rendered;
        if (!ok(vkQueueSubmit(queue,1,&si,fence),"vkQueueSubmit")) return;
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&rendered; pi.swapchainCount=1; pi.pSwapchains=&swapchain; pi.pImageIndices=&index;
        VkResult pr=vkQueuePresentKHR(queue,&pi);
        if (pr!=VK_SUCCESS && pr!=VK_SUBOPTIMAL_KHR) LOGE("vkQueuePresentKHR: %d",pr);
    }

    void cleanup() {
        if (device) vkDeviceWaitIdle(device);
        if (device) {
            if (fence) vkDestroyFence(device,fence,nullptr);
            if (acquired) vkDestroySemaphore(device,acquired,nullptr);
            if (rendered) vkDestroySemaphore(device,rendered,nullptr);
            if (commandPool) vkDestroyCommandPool(device,commandPool,nullptr);
            for (auto f:framebuffers) if(f) vkDestroyFramebuffer(device,f,nullptr);
            for (auto v:views) if(v) vkDestroyImageView(device,v,nullptr);
            if(renderPass) vkDestroyRenderPass(device,renderPass,nullptr);
            if(swapchain) vkDestroySwapchainKHR(device,swapchain,nullptr);
            vkDestroyDevice(device,nullptr);
        }
        if(instance && surface) vkDestroySurfaceKHR(instance,surface,nullptr);
        if(instance) vkDestroyInstance(instance,nullptr);
        device=VK_NULL_HANDLE; instance=VK_NULL_HANDLE;
    }
};

static void handle_cmd(android_app* app, int32_t cmd) {
    auto* state=static_cast<VulkanTest*>(app->userData);
    if (cmd==APP_CMD_INIT_WINDOW && app->window) {
        if (!state->init()) LOGE("Vulkan test initialization FAILED");
    } else if (cmd==APP_CMD_TERM_WINDOW) {
        state->cleanup();
    }
}

void android_main(android_app* app) {
    app_dummy();
    VulkanTest state{}; state.app=app; app->userData=&state; app->onAppCmd=handle_cmd;
    LOGI("Starting Android Vulkan surface/swapchain presentation test");
    while (!app->destroyRequested) {
        int events=0; android_poll_source* source=nullptr;
        while (ALooper_pollOnce(state.device ? 0 : -1,nullptr,&events,(void**)&source)>=0) {
            if (source) source->process(app,source);
            if (app->destroyRequested) break;
        }
        if (state.device) state.draw();
    }
    state.cleanup();
}
