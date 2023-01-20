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

const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

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
    m_sdlWindow = SDL_CreateWindow(
            "vkeng",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            m_windowExtent.width,
            m_windowExtent.height,
            window_flags
            );
    if (m_sdlWindow == NULL) {
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
        SDL_DestroyWindow(m_sdlWindow);
        for (auto view : m_swapChainImageViews) {
            m_vkDevice.destroyImageView(view);
        }
        m_vkDevice.destroyCommandPool(m_commandPool);
        m_vkDevice.destroySwapchainKHR();
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

void VulkanEngine::create_instance() {
    //Initialize DispatchLoaderDynamic stuff (step 1)
    {
        vk::DynamicLoader dl;
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
    }

    //
    // Create an instance
    //
    {
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
        } else {
            instanceCreateInfo.enabledLayerCount = 0;
        }

        //Get SDL2 extensions
        {
            uint count = 0;
            SDL_bool res;
            res = SDL_Vulkan_GetInstanceExtensions(m_sdlWindow, &count, nullptr);
            std::vector<const char *> sdlExtensions(count);
            res = SDL_Vulkan_GetInstanceExtensions(m_sdlWindow, &count, sdlExtensions.data());
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
        } else {
            instanceCreateInfo.enabledLayerCount = 0;
            instanceCreateInfo.pNext = nullptr;
        }

        m_instance = vk::createInstance(instanceCreateInfo);
        std::cout << "Created Vulkan instance." << std::endl;

        //Initialize DispatchLoaderDynamic with the created instance (step 2)
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
    }
}

void VulkanEngine::create_surface() {
    //
    // Create surface
    //
    {
        SDL_bool res = SDL_Vulkan_CreateSurface(m_sdlWindow, m_instance, reinterpret_cast<VkSurfaceKHR *>(&m_vkSurface));
        if (res == SDL_FALSE) {
            std::cout << "SDL_GetError says: " << SDL_GetError() << std::endl;
            throw std::runtime_error("Creating SDL surface failed.");
        }
    }
}

void VulkanEngine::create_debugmessenger() {
    //
    // Set up debug messenger
    //
    if (enableValidationLayers) {
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        populateDebugMessageCreateInfo(debugCreateInfo);

        m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(debugCreateInfo);
    }
}

void VulkanEngine::select_physical_device() {
    //
    // Select physical device
    //
    {
        auto physicalDevices = m_instance.enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw std::runtime_error("Failed to find GPUs with Vulkan support.");
        }

        std::multimap<int, vk::PhysicalDevice> candidates;
        for (const auto &device: physicalDevices) {
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
    }
}

void VulkanEngine::create_logical_device() {
    //
    // Create logical device (vk::Device)
    //
    {
        //Specify queues
        auto indices = findQueueFamilies(m_activeGPU);

        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        float queuePriority = 1.0f; //FIXME: this seems dodgy
        for (uint32_t queueFamily: uniqueQueueFamilies) {
            vk::DeviceQueueCreateInfo info = {};
            info.queueFamilyIndex = queueFamily;
            info.queueCount = 1;
            info.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(info);
        }

        //Specify used device features
        vk::PhysicalDeviceFeatures deviceFeatures = {};

        //Actually create the logical device
        vk::DeviceCreateInfo createInfo = {};
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;

        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } else {
            createInfo.enabledLayerCount = 0;
        }

        m_vkDevice = m_activeGPU.createDevice(createInfo);
        m_graphicsQueue = m_vkDevice.getQueue(indices.graphicsFamily.value(), 0);
        m_presentQueue = m_vkDevice.getQueue(indices.presentFamily.value(), 0);

        std::cout << "Created logical device " << m_vkDevice << "." << std::endl;
    }
}

void VulkanEngine::create_swap_chain() {
    //
    // Create swap chain
    //
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_activeGPU);
        vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (swapChainSupport.capabilities.maxImageCount > 0 &&
            imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR createInfo = {};
        createInfo.surface = m_vkSurface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        //This flag means we render directly to the images, as opposed to some other image as we might if we were doing post-processing (see: vk::ImageUsageFlagBits::eTransferDst)
        createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

        QueueFamilyIndices indices = findQueueFamilies(m_activeGPU);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            createInfo.imageSharingMode = vk::SharingMode::eExclusive;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        //We don't want a transform
        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        //Ignore the alpha channel when blending windows in the compositor
        createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        //This will be used if we need to recreate the swap chain, e.g. if the window is resized. This will be relevant later.
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        m_swapChain = m_vkDevice.createSwapchainKHR(createInfo);
        m_swapChainImages = m_vkDevice.getSwapchainImagesKHR(m_swapChain);
        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainExtent = extent;

        std::cout << "Created swap chain." << std::endl;
    }

    //
    // Create swap chain image views
    //
    {
        m_swapChainImageViews.resize(m_swapChainImages.size());
        for (auto img: m_swapChainImages) {
            vk::ImageViewCreateInfo createInfo = {};
            createInfo.image = img;
            createInfo.viewType = vk::ImageViewType::e2D; //This goes on screen so it's a 2D image
            createInfo.format = m_swapChainImageFormat;

            //Default swizzle mapping
            createInfo.components.r = vk::ComponentSwizzle::eIdentity;
            createInfo.components.g = vk::ComponentSwizzle::eIdentity;
            createInfo.components.b = vk::ComponentSwizzle::eIdentity;
            createInfo.components.a = vk::ComponentSwizzle::eIdentity;

            //These images are "colour targets without any mipmapping levels or multiple layers."
            //For something like VR, we would use multiple layers for the stereoscopic effect.
            createInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            vk::ImageView imageView = m_vkDevice.createImageView(createInfo);
            m_swapChainImageViews.push_back(imageView);
        }
    }
}

void VulkanEngine::create_command_pool_and_buffer() {
    //
    // Create a command pool
    //
    {
        vk::CommandPoolCreateInfo createInfo = {};
        auto indices = findQueueFamilies(m_activeGPU);
        //Make this pool one that submits graphics commands
        createInfo.queueFamilyIndex = indices.graphicsFamily.value();
        //Allow resetting individual buffers in the pool
        createInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_commandPool = m_vkDevice.createCommandPool(createInfo);
    }

    //
    // Create the command buffer
    //
    {
        vk::CommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.pNext = nullptr;

        cmdAllocInfo.commandPool = m_commandPool;
        //Allocate 1 command buffer
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;

        auto buffers = m_vkDevice.allocateCommandBuffers(cmdAllocInfo);
        m_mainCommandBuffer = buffers[0]; //this should be alright as we only allocate one buffer
    }

    std::cout << "Created command pool and command buffer." << std::endl;
}

void VulkanEngine::create_graphics_pipeline() {

}

/*
 * Initializes Vulkan. I honestly couldn't tell you what half this boilerplate does; I wrote it by following along with
 * https://vulkan-tutorial.com
 */
void VulkanEngine::init_vulkan() {
    create_instance();
    create_surface();
    create_debugmessenger();
    select_physical_device();
    create_logical_device();
    create_swap_chain();
    create_command_pool_and_buffer();
    create_graphics_pipeline();
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

bool VulkanEngine::checkDeviceExtensionSupport(const vk::PhysicalDevice & device) {
    auto availExts = device.enumerateDeviceExtensionProperties();
    std::set<std::string> reqExts(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto & ext : availExts) {
        reqExts.erase(ext.extensionName);
    }

    return reqExts.empty();
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
    //It must also support a swap chain to be useful
    if (!checkDeviceExtensionSupport(device)) {
        return 0;
    }
    //And the swap chain must be good enough
    auto swapChainSupport = querySwapChainSupport(device);
    if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty()) {
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

SwapChainSupportDetails VulkanEngine::querySwapChainSupport(const vk::PhysicalDevice &device) {
    SwapChainSupportDetails details;

    details.capabilities = device.getSurfaceCapabilitiesKHR(m_vkSurface);
    details.formats = device.getSurfaceFormatsKHR(m_vkSurface);
    details.presentModes = device.getSurfacePresentModesKHR(m_vkSurface);

    return details;
}

vk::SurfaceFormatKHR VulkanEngine::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    for (const auto & format : availableFormats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }
    return availableFormats[0];
}

vk::PresentModeKHR VulkanEngine::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availableModes) {
    for (const auto & mode : availableModes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            return mode;
        }
    }

    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanEngine::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities) {
    //If the extent size is the magic value of uint32_t max, then we don't have to match the resolution of the window.
    //Otherwise, we have to figure out the actual pixel size of the window, which due to DPI scaling nonsense can be less than trivial.
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    else {
        int width, height;
        SDL_Vulkan_GetDrawableSize(m_sdlWindow, &width, &height);

        vk::Extent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
        };
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }

}

