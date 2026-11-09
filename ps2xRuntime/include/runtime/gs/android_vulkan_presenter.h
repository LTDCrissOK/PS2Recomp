#pragma once
#if defined(__ANDROID__)
#include <android/native_window.h>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

class AndroidVulkanPresenter
{
public:
    AndroidVulkanPresenter() = default;
    ~AndroidVulkanPresenter();
    AndroidVulkanPresenter(const AndroidVulkanPresenter &) = delete;
    AndroidVulkanPresenter &operator=(const AndroidVulkanPresenter &) = delete;

    bool Initialize(ANativeWindow *window);
    bool InitializeFromActivity();
    bool PollEvents();
    bool Present(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height);
    void Shutdown();
    uint32_t Width() const { return m_extent.width; }
    uint32_t Height() const { return m_extent.height; }

private:
    bool CreateSwapchain();
    bool CreateStagingBuffer();
    uint32_t FindMemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;

    struct android_app *m_app = nullptr;
    ANativeWindow *m_window = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_gpu = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_command = VK_NULL_HANDLE;
    VkSemaphore m_acquired = VK_NULL_HANDLE;
    VkSemaphore m_complete = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkBuffer m_staging = VK_NULL_HANDLE;
    VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;
    VkDeviceSize m_stagingSize = 0;
};
#endif
