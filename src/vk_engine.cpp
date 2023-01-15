#include "vk_engine.h"

#include "lib/vk_mem_alloc.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>

#include "vk_types.h"
#include "vk_initializers.h"

const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
};
#ifdef DEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

void VulkanEngine::init() {
    //Initialize SDL window
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    //Create window
    m_window = SDL_CreateWindow(
            "vkeng",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            m_windowExtent.width,
            m_windowExtent.height,
            window_flags
            );
    if (m_window == NULL) {
        std::cout << "Failed to create SDL window. SDL_GetError says " << SDL_GetError() << std::endl;
    }
    else {
        std::cout << "Created SDL window." << std::endl;
    }
    init_vulkan();

    m_isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        SDL_DestroyWindow(m_window);
        m_instance.destroy();
    }
}

void VulkanEngine::draw() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void VulkanEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                bQuit = true;
            else if (e.type == SDL_KEYDOWN) {
                std::cout << "SDL_KEYDOWN: " << e.key.keysym.sym << std::endl;
            }
        }

        draw();
    }
}

void VulkanEngine::init_vulkan() {
    //
    //Create an instance
    //
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "COOL PROJECT 9000";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    appInfo.pEngineName = "super vkeng 3000";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    vk::InstanceCreateInfo instanceCreateInfo;
    instanceCreateInfo.pApplicationInfo = &appInfo;

    std::vector<const char *> requiredExtensions;

    //Get SDL2 extensions
    {
        uint count = 0;
        SDL_bool res;
        res = SDL_Vulkan_GetInstanceExtensions(m_window, &count, NULL);
        std::vector<const char *> sdlExtensions(count);
        res = SDL_Vulkan_GetInstanceExtensions(m_window, &count, sdlExtensions.data());
        if (res == SDL_FALSE) {
            std::cout << "SDL_GetError says: " << SDL_GetError() << std::endl;
            throw std::runtime_error("Getting SDL Vulkan extensions failed.");
        }
        requiredExtensions.insert(requiredExtensions.end(), sdlExtensions.begin(), sdlExtensions.end());
    }
    instanceCreateInfo.enabledExtensionCount = requiredExtensions.size();
    instanceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    //Validation layers
    if (enableValidationLayers) {
        if (!checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested but not available.");
        }
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
        instanceCreateInfo.enabledLayerCount = 0;
    }

    m_instance = vk::createInstance(instanceCreateInfo);

    std::cout << "Created Vulkan instance." << std::endl;
}

bool VulkanEngine::checkValidationLayerSupport() {
    auto availableLayers = vk::enumerateInstanceLayerProperties();

    for (auto layerName : validationLayers) {
        bool layerFound = false;

        for (auto layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName.data()) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}
