#include "vk_engine.h"

#include "lib/vk_mem_alloc.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>
#include <map>
#include <set>

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

static void populateDebugMessageCreateInfo(vk::DebugUtilsMessengerCreateInfoEXT & createInfo) {
    createInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    createInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
    createInfo.setPfnUserCallback(&VulkanEngine::debugCallback);
    createInfo.setPUserData(nullptr);
}

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
        m_vkDevice.destroy();
        m_instance.destroy(m_vkSurface);
        m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
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
    //Initialize DispatchLoaderDynamic stuff (step 1)
    {
        vk::DynamicLoader dl;
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    }

    //
    // Create an instance
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

    //Validation layers
    if (enableValidationLayers) {
        if (!checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested but not available.");
        }
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();

        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    else {
        instanceCreateInfo.enabledLayerCount = 0;
    }

    //Get SDL2 extensions
    {
        uint count = 0;
        SDL_bool res;
        res = SDL_Vulkan_GetInstanceExtensions(m_window, &count, nullptr);
        std::vector<const char *> sdlExtensions(count);
        res = SDL_Vulkan_GetInstanceExtensions(m_window, &count, sdlExtensions.data());
        if (res == SDL_FALSE) {
            std::cout << "SDL_GetError says: " << SDL_GetError() << std::endl;
            throw std::runtime_error("Getting SDL Vulkan extensions failed.");
        }
        requiredExtensions.insert(requiredExtensions.end(), sdlExtensions.begin(), sdlExtensions.end());
    }

    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = requiredExtensions.data();

    // Set up debug messenger for the instance
    if (enableValidationLayers) {
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessageCreateInfo(debugCreateInfo);
        instanceCreateInfo.pNext = &debugCreateInfo;
    }
    else {
        instanceCreateInfo.enabledLayerCount = 0;
        instanceCreateInfo.pNext = nullptr;
    }

    m_instance = vk::createInstance(instanceCreateInfo);
    std::cout << "Created Vulkan instance." << std::endl;

    //Initialize DispatchLoaderDynamic with the created instance (step 2)
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

    //
    // Create surface
    //
    {
        SDL_bool res = SDL_Vulkan_CreateSurface(m_window, m_instance, reinterpret_cast<VkSurfaceKHR *>(&m_vkSurface));
        if (res == SDL_FALSE) {
            std::cout << "SDL_GetError says: " << SDL_GetError() << std::endl;
            throw std::runtime_error("Creating SDL surface failed.");
        }
    }

    //
    // Set up debug messenger
    //
    if (enableValidationLayers) {
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        populateDebugMessageCreateInfo(debugCreateInfo);

        m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(debugCreateInfo);
    }

    //
    // Select physical device
    //
    auto physicalDevices = m_instance.enumeratePhysicalDevices();
    if (physicalDevices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }

    std::multimap<int, vk::PhysicalDevice> candidates;
    for (const auto & device : physicalDevices) {
        int score = scoreDevice(device);
        candidates.insert(std::make_pair(score, device));
    }

    if (candidates.rbegin()->first > 0) {
        m_activeGPU = candidates.rbegin()->second;
    }

    if (!m_activeGPU) {
        throw std::runtime_error("Failed to find a GPU that meets minimum requirements.");
    }

    std::cout << "Using physical device " << m_activeGPU.getProperties().deviceName << "." << std::endl;

    //
    // Create logical device (vk::Device)
    //
    //Specify queues
    auto indices = findQueueFamilies(m_activeGPU);

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f; //FIXME: this seems dodgy
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo info;
        info.queueFamilyIndex = queueFamily;
        info.queueCount = 1;
        info.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(info);
    }

    //Specify used device features
    vk::PhysicalDeviceFeatures deviceFeatures;

    //Actually create the logical device
    vk::DeviceCreateInfo createInfo;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = 0;
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    m_vkDevice = m_activeGPU.createDevice(createInfo);
    m_graphicsQueue = m_vkDevice.getQueue(indices.graphicsFamily.value(), 0);
    m_presentQueue = m_vkDevice.getQueue(indices.presentFamily.value(), 0);

    std::cout << "Created logical device " << m_vkDevice << "." << std::endl;

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

VkBool32 VulkanEngine::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                     VkDebugUtilsMessageTypeFlagsEXT messageType,
                                     const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

/*
 * Some crappy scoring system inspired by/stolen from vulkan-tutorial.com
 * Really I should just use VulkanBootstrap which has a better selector
 * made by smarter people willing to spend more time on writing a selector.
 */
int VulkanEngine::scoreDevice(const vk::PhysicalDevice &device) {
    auto deviceProperties = device.getProperties();
    auto deviceFeatures = device.getFeatures();
    int score = 0;

    //The device must have a geometry shader to be useful
    if (!deviceFeatures.geometryShader) {
        return 0;
    }
    //The device must support a queue family with VK_QUEUE_GRAPHICS_BIT to be useful
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
        return 0;
    }

    score += deviceProperties.limits.maxImageDimension2D;

    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score *= 2;
    }

    return score;
}

QueueFamilyIndices VulkanEngine::findQueueFamilies(const vk::PhysicalDevice &device) {
    QueueFamilyIndices indices;

    auto families = device.getQueueFamilyProperties();
    int i = 0;
    for (auto it = families.begin(); it != families.end(); it++, i++) {
        if (it->queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }
        if (device.getSurfaceSupportKHR(i, m_vkSurface)) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}
