#ifndef VKENG_VK_ENGINE_H
#define VKENG_VK_ENGINE_H


#include <optional>
#include "vk_types.h"

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

class VulkanEngine {
public:
    //
    // Public methods
    //

    //Initialize engine
    void init();

    //Shut down and clean up
    void cleanup();

    //Draw loop
    void draw();

    //Main loop
    void run();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData);

private:
    //
    // Private members
    //
    bool m_isInitialized{false};
    int m_frameNumber {0};
    struct SDL_Window* m_sdlWindow{nullptr};

    // Vulkan members and handles
    vk::Extent2D m_windowExtent{640, 480};
    vk::Instance m_instance;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    vk::PhysicalDevice m_activeGPU;
    vk::Device m_vkDevice;
    vk::SurfaceKHR m_vkSurface;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;
    vk::SwapchainKHR m_swapChain;
    std::vector<vk::Image> m_swapChainImages;
    vk::Format m_swapChainImageFormat;
    vk::Extent2D m_swapChainExtent;
    std::vector<vk::ImageView> m_swapChainImageViews;
    vk::CommandPool m_commandPool;
    vk::CommandBuffer m_mainCommandBuffer;
    vk::RenderPass m_renderPass;
    std::vector<vk::Framebuffer> m_framebuffers;
    vk::Semaphore m_presentSemaphore, m_renderSemaphore;
    vk::Fence m_renderFence;

    vk::PipelineLayout m_trianglePipelineLayout;
    vk::Pipeline m_trianglePipeline;

    //
    // Private methods
    //

    void init_vulkan();

    bool checkValidationLayerSupport();

    bool checkDeviceExtensionSupport(const vk::PhysicalDevice & device);

    int scoreDevice(const vk::PhysicalDevice & device);

    QueueFamilyIndices findQueueFamilies(const vk::PhysicalDevice & device);

    //Swap chain functions
    SwapChainSupportDetails querySwapChainSupport(const vk::PhysicalDevice & device);
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availableModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR & capabilities);


    void create_instance();

    void create_surface();

    void create_debugmessenger();

    void select_physical_device();

    void create_logical_device();

    void create_swap_chain();

    void create_command_pool_and_buffer();

    void init_default_render_pass();

    void init_framebuffers();

    void init_sync_structures();

    void init_pipelines();

    //This will throw if the shader modules fail to load.
    vk::ShaderModule load_shader_module(const char * filePath);
};

//sweet lord what is happening in here??
//Thank you vkguide.dev
class PipelineBuilder {
public:
    std::vector<vk::PipelineShaderStageCreateInfo> m_shaderStageInfos;
    vk::PipelineVertexInputStateCreateInfo m_vertexInputInfo;
    vk::PipelineInputAssemblyStateCreateInfo m_inputAssemblyInfo;
    vk::Viewport m_viewport;
    vk::Rect2D m_scissor;
    vk::PipelineRasterizationStateCreateInfo m_rasterizerInfo;
    vk::PipelineColorBlendAttachmentState m_colorBlendAttachmentState;
    vk::PipelineMultisampleStateCreateInfo m_multisampleInfo;
    vk::PipelineLayout m_pipelineLayout;

    vk::Pipeline build_pipeline(vk::Device device, vk::RenderPass pass);

};

#endif //VKENG_VK_ENGINE_H
