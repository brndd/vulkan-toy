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

class VulkanEngine {
public:
    //
    // Members
    //
    bool m_isInitialized{false};
    int m_frameNumber {0};
    struct SDL_Window* m_window{nullptr};

    // Vulkan members and handles
    vk::Extent2D m_windowExtent{640, 480};
    vk::Instance m_instance;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    vk::PhysicalDevice m_activeGPU;
    vk::Device m_vkDevice;
    vk::SurfaceKHR m_vkSurface;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;



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
    // Private methods
    //

    void init_vulkan();

    bool checkValidationLayerSupport();

    int scoreDevice(const vk::PhysicalDevice & device);

    QueueFamilyIndices findQueueFamilies(const vk::PhysicalDevice & device);
};


#endif //VKENG_VK_ENGINE_H
