#ifndef VKENG_VK_ENGINE_H
#define VKENG_VK_ENGINE_H


#include "vk_types.h"

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

private:

    //
    // Private methods
    //

    void init_vulkan();

    bool checkValidationLayerSupport();
};


#endif //VKENG_VK_ENGINE_H
