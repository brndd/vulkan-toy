#ifndef VKENG_VK_ENGINE_H
#define VKENG_VK_ENGINE_H


#include <optional>
#include <deque>
#include <glm/glm.hpp>
#include <chrono>
#include "vk_types.h"
#include "vk_mesh.h"

const int FRAMES_IN_FLIGHT = 2;

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

/*
 * Simple (and somewhat inefficient) deletion queue for cleaning up Vulkan objects when the engine exists.
 * Heavily inspired by vkguide.dev (like everything else here)
 */
struct DeletionQueue {
    std::deque<std::function<void()>> deleters;

    void push_function(std::function<void()> && func) {
        deleters.push_back(func);
    }

    void flush() {
        for (auto it = deleters.rbegin(); it != deleters.rend(); it++) {
            (*it)();
        }
        deleters.clear();
    }
};

struct MeshPushConstants {
    glm::vec4 data;
    glm::mat4 render_matrix;
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
    bool m_isInitialized = false;
    int m_frameNumber = 0;
    float m_simulationTime = 0.0f; //Simulation time in seconds
    struct SDL_Window* m_sdlWindow = nullptr;
    int m_selectedShader = 0;
    bool m_framebufferResized = false;
    DeletionQueue m_mainDeletionQueue;
    DeletionQueue m_pipelineDeletionQueue;
    vma::Allocator m_allocator;

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
    std::vector<vk::CommandBuffer> m_commandBuffers;
    vk::RenderPass m_renderPass;
    std::vector<vk::Framebuffer> m_swapChainFramebuffers;
    std::vector<vk::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::Fence> m_inFlightFences;

    vk::PipelineLayout m_meshPipelineLayout;

    vk::Pipeline m_meshPipeline;
    Mesh m_triangleMesh;

    //
    // Private methods
    //

    void initVulkan();

    bool checkValidationLayerSupport();

    bool checkDeviceExtensionSupport(const vk::PhysicalDevice & device);

    int scoreDevice(const vk::PhysicalDevice & device);

    QueueFamilyIndices findQueueFamilies(const vk::PhysicalDevice & device);

    //Swap chain functions
    SwapChainSupportDetails querySwapChainSupport(const vk::PhysicalDevice & device);
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availableModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR & capabilities);


    void createInstance();

    void createSurface();

    void createDebugMessenger();

    void selectPhysicalDevice();

    void createLogicalDevice();

    void createSwapChain();

    void createCommandPoolAndBuffers();

    void createDefaultRenderPass();

    void createFramebuffers();

    void createSyncStructures();

    void createPipelines();

    void recreatePipelines();

    void recreateSwapChain();

    void cleanupSwapChain();


    //This will throw if the shader modules fail to load.
    vk::ShaderModule loadShaderModule(const char * filePath);

    void loadMeshes();
    void uploadMesh(Mesh &mesh);
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

    vk::Pipeline buildPipeline(vk::Device device, vk::RenderPass pass);

};

#endif //VKENG_VK_ENGINE_H
