#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <glm/gtx/transform.hpp>

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>
#include <map>
#include <set>
#include <fstream>

#include "vk_types.h"
#include "vk_initializers.h"

//Global dispatch loader singleton
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

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
    SDL_WindowFlags window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    //Create window
    m_sdlWindow = SDL_CreateWindow(
            "vkeng",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            static_cast<int>(m_windowExtent.width),
            static_cast<int>(m_windowExtent.height),
            window_flags
            );
    if (m_sdlWindow == NULL) {
        std::cout << "Failed to create SDL window. SDL_GetError says " << SDL_GetError() << std::endl;
    }
    else {
        std::cout << "Created SDL window." << std::endl;
    }

    initVulkan();

    createSyncStructures();

    createDescriptors();

    createPipelines();

    loadMeshes();

    initScene();

    m_isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        //Wait for the device to finish rendering before cleaning up
        m_vkDevice.waitIdle();

        //Destroy all objects in the deletion queues
        m_pipelineDeletionQueue.flush();
        m_mainDeletionQueue.flush();

        //Swap chain and swap chain accessories are separate so they can be recreated if necessary.
        cleanupSwapChain();

        //Destroy objects that for some reason aren't in the deletion queue
        m_allocator.destroy();

        m_vkDevice.destroy();
        m_instance.destroy(m_vkSurface);
        m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
        m_instance.destroy();
        SDL_DestroyWindow(m_sdlWindow);
    }
}

void VulkanEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        auto start = std::chrono::high_resolution_clock::now();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                bQuit = true;
            } else if (e.type == SDL_KEYDOWN) {
                std::cout << "[SDL_KEYDOWN] sym: " << e.key.keysym.sym << " code: " << e.key.keysym.scancode
                          << std::endl;
                if (e.key.keysym.scancode == SDL_SCANCODE_SPACE) {
                    m_selectedShader++;
                    if (m_selectedShader > 1) {
                        m_selectedShader = 0;
                    }
                }
            }
            else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    std::cout << "[SDL_WINDOWEVENT] Resizing window..." << std::endl;
                    int width = e.window.data1;
                    int height = e.window.data2;
                    m_windowExtent.width = width;
                    m_windowExtent.height = height;
                    m_framebufferResized = true;
                }
            }
        }

        draw();
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<float>>(end - start).count();
        m_simulationTime += elapsedTime;
    }
}

void VulkanEngine::draw() {
    auto frame = getCurrentFrame();

    //Wait until the GPU has rendered the previous frame, with a timeout of 1 second.
    auto waitResult = m_vkDevice.waitForFences(frame.inFlightFence, true, S_TO_NS(1));
    if (waitResult == vk::Result::eTimeout) {
        std::cout << "Waiting for fences timed out!" << std::endl;
    }

    //Request image from swapchain with one second timeout.
    auto [nextImageResult, swapChainImgIndex] = m_vkDevice.acquireNextImageKHR(m_swapChain, S_TO_NS(1), frame.imageAvailableSemaphore);
    if (nextImageResult == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapChain();
        return;
    }
    else if (nextImageResult != vk::Result::eSuccess) {
        vk::throwResultException(nextImageResult, "Failed to acquire swap chain image.");
    }

    m_vkDevice.resetFences(frame.inFlightFence);

    //Reset the command buffer now that commands are done executing.
    auto & cmd = frame.mainCommandBuffer;
    cmd.reset();

    //Tell Vulkan we will only use this once
    vk::CommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmd.begin(cmdBeginInfo);

    //Clear screen to blue
    vk::ClearValue clearValue = {};
    float flash = abs(sin(m_simulationTime));
    const std::array<float, 4> cols = {0.0f, 0.0f, flash, 1.0f};
    clearValue.color = {cols};

    //Clear depth buffer
    vk::ClearValue depthClear = {};
    depthClear.depthStencil.depth = 1.0f;

    //Start the main render pass
    vk::RenderPassBeginInfo rpInfo = {};
    rpInfo.renderPass = m_renderPass;
    rpInfo.renderArea.offset.x = 0;
    rpInfo.renderArea.offset.y = 0;
    rpInfo.renderArea.extent = m_swapChainExtent;
    rpInfo.framebuffer = m_swapChainFramebuffers[swapChainImgIndex];

    //connect clear values
    rpInfo.clearValueCount = 2;
    vk::ClearValue clearValues[2] = {clearValue, depthClear};
    rpInfo.pClearValues = &clearValues[0];


    //
    //Render commands go here
    //
    cmd.beginRenderPass(rpInfo, vk::SubpassContents::eInline);

    drawObjects(cmd, m_renderables.data(), m_renderables.size());

    //Finalize the render pass
    cmd.endRenderPass();
    //Finalize the command buffer (can no longer add commands, but it can be executed)
    cmd.end();


    //Submit command buffer to GPU
    vk::SubmitInfo submitInfo = {};
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.setWaitSemaphores(frame.imageAvailableSemaphore);
    submitInfo.setSignalSemaphores(frame.renderFinishedSemaphore);
    submitInfo.setCommandBuffers(cmd);

    //Submit command buffer to the queue and execute it.
    //m_inFlightFences will now block until the graphic commands finish execution.
    m_graphicsQueue.submit(submitInfo, frame.inFlightFence);

    //Finally, display the image on the screen.
    vk::PresentInfoKHR presentInfo = {};
    presentInfo.setSwapchains(m_swapChain);
    presentInfo.setWaitSemaphores(frame.renderFinishedSemaphore);
    presentInfo.setImageIndices(swapChainImgIndex);
    auto presentResult = m_graphicsQueue.presentKHR(presentInfo);
    if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR || m_framebufferResized) {
        m_framebufferResized = false;
        recreateSwapChain();
        recreatePipelines();
    }
    else if (presentResult != vk::Result::eSuccess) {
        vk::throwResultException(nextImageResult, "Failed to present swap chain image.");
    }

    m_frameNumber++;
}

void VulkanEngine::drawObjects(vk::CommandBuffer cmd, RenderObject *first, int count) {
    glm::vec3 camPos = {0.0f, -6.0f, -10.0f};
    glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
    projection[1][1] *= -1;

    //Fill the camera data struct...
    GPUCameraData camData;
    camData.view = view;
    camData.projection = projection;
    camData.viewProjection = projection * view;

    //...and copy it into the buffer
    GPUCameraData * data = static_cast<GPUCameraData *>(m_allocator.mapMemory(getCurrentFrame().cameraBuffer.allocation));
    memcpy(data, &camData, sizeof(GPUCameraData));
    m_allocator.unmapMemory(getCurrentFrame().cameraBuffer.allocation);

    Mesh* lastMesh = nullptr;
    Material* lastMaterial = nullptr;

    //TODO: sort array by pipeline pointer to reduce number of binds, maybe?
    for (int i = 0; i < count; i++) {
        RenderObject& object = first[i];

        //Only bind the pipeline if it doesn't match the already bound one
        if (object.material != lastMaterial) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, object.material->pipeline);
            lastMaterial = object.material;
            //Bind the descriptor set when changing pipeline
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.material->pipelineLayout, 0, getCurrentFrame().globalDescriptor, nullptr);
        }

        MeshPushConstants constants;
        constants.renderMatrix = object.transformMatrix;

        cmd.pushConstants(object.material->pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshPushConstants), &constants);

        //Only bind the mesh if it doesn't match the already bound one
        if (object.mesh != lastMesh) {
            vk::DeviceSize offset = 0;
            cmd.bindVertexBuffers(0, 1, &object.mesh->vertexBuffer.buffer, &offset);
            lastMesh = object.mesh;
        }

        cmd.draw(object.mesh->vertices.size(), 1, 0, 0);
    }
}

void VulkanEngine::createInstance() {
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
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        if (enableValidationLayers) {
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

void VulkanEngine::createSurface() {
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

void VulkanEngine::createDebugMessenger() {
    //
    // Set up debug messenger
    //
    if (enableValidationLayers) {
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
        populateDebugMessageCreateInfo(debugCreateInfo);

        m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(debugCreateInfo);
    }
}

void VulkanEngine::selectPhysicalDevice() {
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

void VulkanEngine::createLogicalDevice() {
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

void VulkanEngine::createSwapChain() {
    //
    // Create swap chain
    //
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_activeGPU);
        vk::SurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        vk::PresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        std::cout << "Swap chain extent size: " << extent.width << ", " << extent.height << std::endl;
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

        //Create swapchain and also push it into the deletion queue
        m_swapChain = m_vkDevice.createSwapchainKHR(createInfo);

        m_swapChainImages = m_vkDevice.getSwapchainImagesKHR(m_swapChain);
        m_swapChainImageFormat = surfaceFormat.format;
        m_swapChainExtent = extent;

        std::cout << "Created swap chain." << std::endl;
    }

    //
    // Create swap chain image views
    //
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

        m_swapChainImageViews.emplace_back(m_vkDevice.createImageView(createInfo));
    }
    std::cout << "Created " << m_swapChainImageViews.size() << " swap chain image views." << std::endl;

    //
    // Create depth buffers
    //
    vk::Extent3D depthImageExtent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
    m_depthFormat = vk::Format::eD32Sfloat;

    vk::ImageCreateInfo depthImgInfo = vkinit::imageCreateInfo(m_depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment, depthImageExtent);
    //We want the depth image in GPU local memory
    vma::AllocationCreateInfo depthAllocInfo = {};
    depthAllocInfo.usage = vma::MemoryUsage::eGpuOnly;
    depthAllocInfo.requiredFlags = vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal);

    //Allocate and create the depth buffer image
    auto imagePair = m_allocator.createImage(depthImgInfo, depthAllocInfo);
    m_depthImage.image = imagePair.first;
    m_depthImage.allocation = imagePair.second;

    vk::ImageViewCreateInfo depthViewInfo = vkinit::imageViewCreateInfo(m_depthFormat, m_depthImage.image, vk::ImageAspectFlagBits::eDepth);
    m_depthImageView = m_vkDevice.createImageView(depthViewInfo);
}

void VulkanEngine::createCommandPoolAndBuffers() {
    // Create a command pool and primary command buffer for each frame in flight

    vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    auto createInfo = vkinit::commandPoolCreateInfo(findQueueFamilies(m_activeGPU).graphicsFamily.value(), flags);

    for (auto & frame : m_frames) {
        auto pool = m_vkDevice.createCommandPool(createInfo);
        frame.commandPool = pool;

        vk::CommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.commandPool = pool;
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        auto buffer = m_vkDevice.allocateCommandBuffers(cmdAllocInfo);
        frame.mainCommandBuffer = buffer[0];

        m_mainDeletionQueue.pushFunction([=]() {
            m_vkDevice.destroyCommandPool(pool);
        });
    }

    std::cout << "Created command pool and command buffer." << std::endl;
}

void VulkanEngine::createDefaultRenderPass() {
    //Quoting vkguide.dev:
    //The image life will go something like this:
    //UNDEFINED -> RenderPass Begins -> Subpass 0 begins (Transition to Attachment Optimal) -> Subpass 0 renders -> Subpass 0 ends -> Renderpass Ends (Transitions to Present Source)

    vk::AttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapChainImageFormat;
    //Only 1 sample as we won't be doing MSAA (yet)
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    //Clear when attachment is loaded
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    //Store when render pass ends
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    //Don't care about these (yet?)
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

    //Don't know or care about the starting layout
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;

    //After the render pass is through, the image has to be presentable to the screen
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentRef = {};
    //This will index into the pAttachments array in the parent render pass
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    //Create depth buffer attachment
    vk::AttachmentDescription depthAttachment = {};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    //Create 1 subpass (the minimum)
    vk::SubpassDependency colorDependency = {};
    colorDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    colorDependency.dstSubpass = 0;
    colorDependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    colorDependency.srcAccessMask = vk::AccessFlagBits::eNone;
    colorDependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    colorDependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    //"This dependency tells Vulkan that the depth attachment in a renderpass
    // cannot be used before previous renderpasses have finished using it."
    vk::SubpassDependency depthDependency = {};
    depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    depthDependency.dstSubpass = 0;
    depthDependency.srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    depthDependency.srcAccessMask = vk::AccessFlagBits::eNone;
    depthDependency.dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    depthDependency.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    vk::SubpassDescription subpass = {};
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    //Finally, create the actual render pass
    vk::RenderPassCreateInfo createInfo = {};
    vk::SubpassDependency dependencies[2] = {colorDependency, depthDependency};
    vk::AttachmentDescription attachments[2] = {colorAttachment, depthAttachment};
    createInfo.attachmentCount = std::size(attachments);
    createInfo.pAttachments = &attachments[0];
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = std::size(dependencies);
    createInfo.pDependencies = &dependencies[0];

    m_renderPass = m_vkDevice.createRenderPass(createInfo);
    m_mainDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyRenderPass(m_renderPass);
    });
}

void VulkanEngine::createFramebuffers() {
    vk::FramebufferCreateInfo fbInfo = {};
    fbInfo.pNext = nullptr;
    fbInfo.renderPass = m_renderPass;
    fbInfo.attachmentCount = 2;
    fbInfo.width = m_swapChainExtent.width;
    fbInfo.height = m_swapChainExtent.height;
    fbInfo.layers = 1;

    //Create a framebuffer for each swap chain image view
    for (const auto & view : m_swapChainImageViews) {
        vk::ImageView attachments[2];
        attachments[0] = view;
        attachments[1] = m_depthImageView;
        fbInfo.pAttachments = attachments;
        auto buf = m_vkDevice.createFramebuffer(fbInfo);
        m_swapChainFramebuffers.push_back(buf);
    }
    std::cout << "Initialized " << m_swapChainFramebuffers.size() << " framebuffers." << std::endl;
}

void VulkanEngine::createSyncStructures() {
    vk::FenceCreateInfo fenceInfo = {};
    //This allows us to wait on the fence on first use
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;

    vk::SemaphoreCreateInfo semaphoreInfo = {};

    for (auto & frame : m_frames) {
        frame.inFlightFence = m_vkDevice.createFence(fenceInfo);
        frame.imageAvailableSemaphore = m_vkDevice.createSemaphore(semaphoreInfo);
        frame.renderFinishedSemaphore = m_vkDevice.createSemaphore(semaphoreInfo);

        m_mainDeletionQueue.pushFunction([=]() {
            m_vkDevice.destroyFence(frame.inFlightFence);
            m_vkDevice.destroySemaphore(frame.imageAvailableSemaphore);
            m_vkDevice.destroySemaphore(frame.renderFinishedSemaphore);
        });
    }
}

void VulkanEngine::createDescriptors() {
    vk::DescriptorSetLayoutBinding camBufferBinding = {};
    camBufferBinding.binding = 0;
    camBufferBinding.descriptorCount = 1;
    camBufferBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    camBufferBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo setInfo = {};
    setInfo.setBindings(camBufferBinding);

    m_globalDescriptorSetLayout = m_vkDevice.createDescriptorSetLayout(setInfo);
    m_mainDeletionQueue.pushFunction([=] () {
        m_vkDevice.destroyDescriptorSetLayout(m_globalDescriptorSetLayout);
    });

    //Create a descriptor pool to hold 10 uniform buffers
    std::vector<vk::DescriptorPoolSize> sizes = {
            { vk::DescriptorType::eUniformBuffer, 10 }
    };

    vk::DescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.maxSets = 10;
    poolCreateInfo.setPoolSizes(sizes);

    m_descriptorPool = m_vkDevice.createDescriptorPool(poolCreateInfo);
    m_mainDeletionQueue.pushFunction([=] () {
        m_vkDevice.destroyDescriptorPool(m_descriptorPool);
    });

    //Create buffer for camera data
    for (auto & frame : m_frames) {
        frame.cameraBuffer = createBuffer(sizeof(GPUCameraData), vk::BufferUsageFlagBits::eUniformBuffer, vma::MemoryUsage::eCpuToGpu);

        m_mainDeletionQueue.pushFunction([=] () {
            m_allocator.destroyBuffer(frame.cameraBuffer.buffer, frame.cameraBuffer.allocation);
        });

        //Allocate one descriptor set for each frame
        vk::DescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.descriptorPool = m_descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.setSetLayouts(m_globalDescriptorSetLayout);

        frame.globalDescriptor = m_vkDevice.allocateDescriptorSets(allocateInfo)[0]; //We only get one result from this call

        //Point the descriptor to the camera buffer
        vk::DescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = frame.cameraBuffer.buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GPUCameraData);

        vk::WriteDescriptorSet setWrite = {};
        setWrite.dstBinding = 0;
        setWrite.dstSet = frame.globalDescriptor;
        setWrite.descriptorCount = 1;
        setWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        setWrite.setBufferInfo(bufferInfo);

        m_vkDevice.updateDescriptorSets(setWrite, nullptr);
    }

}

/*
 * Initializes Vulkan. I honestly couldn't tell you what half this boilerplate does; I wrote it by following along with
 * https://vulkan-tutorial.com and https://vkguide.dev, and it's... it's a lot to take in.
 */
void VulkanEngine::initVulkan() {
    createInstance();
    createSurface();
    createDebugMessenger();
    selectPhysicalDevice();
    createLogicalDevice();

    //Initialize memory allocator
    vma::AllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_activeGPU;
    allocatorInfo.device = m_vkDevice;
    allocatorInfo.instance = m_instance;
    m_allocator = vma::createAllocator(allocatorInfo);

    createSwapChain();
    createCommandPoolAndBuffers();
    createDefaultRenderPass();
    createFramebuffers();
}

void VulkanEngine::initScene() {
    RenderObject monkey;
    monkey.mesh = getMesh("monkey");
    monkey.material = getMaterial("defaultmesh");
    monkey.transformMatrix = glm::mat4{1.0f};
    m_renderables.push_back(monkey);

    for (int i = -20; i <= 20; i++) {
        for (int j = -20; j <= 20; j++) {
            RenderObject rect;
            rect.mesh = getMesh("rectangle");
            rect.material = getMaterial("defaultmesh");
            glm::mat4 translation = glm::translate(glm::mat4{1.0}, glm::vec3{i, 0, j});
            glm::mat4 scale = glm::scale(glm::mat4{1.0}, glm::vec3(0.2, 0.2, 0.2));
            rect.transformMatrix = translation * scale;
            m_renderables.push_back(rect);
        }
    }
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
//    for (const auto & mode : availableModes) {
//        if (mode == vk::PresentModeKHR::eMailbox) {
//            return mode;
//        }
//    }

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

vk::ShaderModule VulkanEngine::loadShaderModule(const char *filePath) {
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file.");
    }

    size_t fileSize = file.tellg(); //File size in bytes
    std::vector<uint32_t> code(fileSize / sizeof(uint32_t));

    //Load the entire file into the buffer and close the file
    file.seekg(0);
    file.read((char*) code.data(), fileSize);
    file.close();

    vk::ShaderModuleCreateInfo info = {};
    info.setCode(code);

    vk::ShaderModule module = m_vkDevice.createShaderModule(info); //FIXME: ensure that any resources are cleaned up if this throws (not sure if necessary)
    return module;
}

//TODO: use vk::PipelineCache to speed up rebuilding these
void VulkanEngine::createPipelines() {
    vk::ShaderModule triangleFragShader = loadShaderModule("shaders/hellotriangle.frag.spv");
    //Load mesh vertex shader
    vk::ShaderModule meshVertShader = loadShaderModule("shaders/tri_mesh.vert.spv");
    std::cout << "Loaded shaders." << std::endl;

    PipelineBuilder pipelineBuilder;

    //This controls how to read vertices from the vertex buffers. We aren't using them yet.
    pipelineBuilder.m_vertexInputInfo = vkinit::pipelineVertexInputStateCreateInfo();

    //This is the configuration for drawing triangle lists, strips, or individual points
    //We are just drawing a triangle list.
    pipelineBuilder.m_inputAssemblyInfo = vkinit::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology::eTriangleList);

    //Build viewport and scissor
    pipelineBuilder.m_viewport.x = 0.0f;
    pipelineBuilder.m_viewport.y = 0.0f;
    pipelineBuilder.m_viewport.width = static_cast<float>(m_swapChainExtent.width);
    pipelineBuilder.m_viewport.height = static_cast<float>(m_swapChainExtent.height);
    pipelineBuilder.m_viewport.minDepth = 0.0f;
    pipelineBuilder.m_viewport.maxDepth = 1.0f;

    pipelineBuilder.m_scissor.offset = vk::Offset2D{0, 0};
    pipelineBuilder.m_scissor.extent = m_swapChainExtent; //FIXME: do we need the "real" hiDPI-aware extent here?

    //Configure the rasterizer to draw filled rectangles
    pipelineBuilder.m_rasterizerInfo = vkinit::pipelineRasterizationStateCreateInfo(vk::PolygonMode::eFill);

    //no multisampling (for now)
    pipelineBuilder.m_multisampleInfo = vkinit::multisampleStateCreateInfo();

    //a single blend attachment with no blending, writing to RGBA
    pipelineBuilder.m_colorBlendAttachmentState = vkinit::pipelineColorBlendAttachmentState();

    //Build the mesh pipeline
    VertexInputDescription vertexDescription = Vertex::getVertexDescription();

    vk::PipelineLayoutCreateInfo meshPipelineInfo = vkinit::pipelineLayoutCreateInfo();

    //Set up push constants
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MeshPushConstants);
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    meshPipelineInfo.setPushConstantRanges(pushConstantRange);

    //Hook up global descriptor set
    meshPipelineInfo.setSetLayouts(m_globalDescriptorSetLayout);

    m_meshPipelineLayout = m_vkDevice.createPipelineLayout(meshPipelineInfo); //queued for deletion at the bottom of this func

    //Connect the builder vertex input info to the one we got from Vertex::
    pipelineBuilder.m_vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);
    pipelineBuilder.m_vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

    //Add shaders
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, meshVertShader));
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, triangleFragShader));

    //Hook the new layout into the pipelineBuilder
    pipelineBuilder.m_pipelineLayout = m_meshPipelineLayout;

    //Default depth testing
    pipelineBuilder.m_depthStencil = vkinit::depthStencilStateCreateInfo(true, true, vk::CompareOp::eLessOrEqual);

    //Finally, build the pipeline
    m_meshPipeline = pipelineBuilder.buildPipeline(m_vkDevice, m_renderPass);

    //Add the pipeline to our materials
    createMaterial(m_meshPipeline, m_meshPipelineLayout, "defaultmesh");

    //Destroy shader modules
    m_vkDevice.destroyShaderModule(meshVertShader);
    m_vkDevice.destroyShaderModule(triangleFragShader);

    //Queue destruction of pipelines
    m_pipelineDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyPipeline(m_meshPipeline);

        m_vkDevice.destroyPipelineLayout(m_meshPipelineLayout);
    });
}

void VulkanEngine::loadMeshes() {
    //Hardcode a triangle mesh
    Mesh rect;
    rect.vertices.resize(4);
    rect.vertices[0].position = {-0.5f, -0.5f, 0.0f};
    rect.vertices[1].position = {0.5f, -0.5f, 0.0f};
    rect.vertices[2].position = {0.5f, 0.5f, 0.0f};
    rect.vertices[3].position = {-0.5f, 0.5f, 0.0f};
    rect.vertices[0].color = {1.0f, 0.0f, 1.0f};
    rect.vertices[1].color = {0.0f, 1.0f, 0.0f};
    rect.vertices[2].color = {1.0f, 0.0f, 1.0f};
    rect.vertices[3].color = {0.0f, 1.0f, 0.0f};
    rect.indices = {0, 1, 2, 2, 3, 0};
    //Don't need normals (yet)

    //Monke mesh
    Mesh monke;
    monke.loadFromObj("data/assets/monkey_smooth.obj");

    uploadMesh(rect);
    uploadMesh(monke);

    m_meshes["monkey"] = monke;
    m_meshes["rectangle"] = rect;
}

void VulkanEngine::uploadMesh(Mesh &mesh) {
    vk::BufferCreateInfo bufferInfo = {};
    bufferInfo.size = mesh.vertices.size() * sizeof(Vertex);
    bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;

    //Tell VMA that this should be writable by CPU, readable by GPU
    vma::AllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage  = vma::MemoryUsage::eCpuToGpu;

    //Allocate the vertex buffer
    auto vertexPair = m_allocator.createBuffer(bufferInfo, vmaAllocInfo);
    mesh.vertexBuffer.buffer = vertexPair.first;
    mesh.vertexBuffer.allocation = vertexPair.second;

    //And the index buffer
    if (!mesh.indices.empty()) {
        bufferInfo.size = mesh.indices.size() * sizeof(uint16_t);
        bufferInfo.usage = vk::BufferUsageFlagBits::eIndexBuffer;
        auto indexPair = m_allocator.createBuffer(bufferInfo, vmaAllocInfo);
        mesh.indexBuffer.buffer = indexPair.first;
        mesh.indexBuffer.allocation = indexPair.second;
    }

    //And queue their deletion
    m_mainDeletionQueue.pushFunction([=]() {
        m_allocator.destroyBuffer(mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
    });
    if (!mesh.indices.empty()) {
        m_mainDeletionQueue.pushFunction([=]() {
            m_allocator.destroyBuffer(mesh.indexBuffer.buffer, mesh.indexBuffer.allocation);
        });
    }

    //Copy vertex vertexData
    Vertex *vertexData = static_cast<Vertex *>(m_allocator.mapMemory(mesh.vertexBuffer.allocation));
    std::copy(mesh.vertices.begin(), mesh.vertices.end(), vertexData);
    //std::memcpy(vertexData, mesh.vertices.vertexData(), mesh.vertices.size() * sizeof(Vertex));
    m_allocator.unmapMemory(mesh.vertexBuffer.allocation);

    //Copy index vertexData
    if (!mesh.indices.empty()) {
        uint16_t *indexData = static_cast<uint16_t *>(m_allocator.mapMemory(mesh.indexBuffer.allocation));
        std::copy(mesh.indices.begin(), mesh.indices.end(), indexData);
        m_allocator.unmapMemory(mesh.indexBuffer.allocation);
    }
}

void VulkanEngine::recreateSwapChain() {
    std::cout << "Recreating swap chain." << std::endl;
    m_vkDevice.waitIdle();

    cleanupSwapChain();
    createSwapChain();
    createFramebuffers();
}

void VulkanEngine::cleanupSwapChain() {
    m_vkDevice.destroyImageView(m_depthImageView);
    m_allocator.destroyImage(m_depthImage.image, m_depthImage.allocation);

    for (auto buf : m_swapChainFramebuffers) {
        m_vkDevice.destroyFramebuffer(buf);
    }
    m_swapChainFramebuffers.clear();
    for (auto view : m_swapChainImageViews) {
        m_vkDevice.destroyImageView(view);
    }
    m_swapChainImageViews.clear();
    m_vkDevice.destroySwapchainKHR(m_swapChain);
}

void VulkanEngine::recreatePipelines() {
    m_pipelineDeletionQueue.flush();
    createPipelines();
}

Material *VulkanEngine::createMaterial(vk::Pipeline pipeline, vk::PipelineLayout layout, const std::string &name) {
    Material mat;
    mat.pipeline = pipeline;
    mat.pipelineLayout = layout;
    m_materials[name] = mat;
    return &m_materials[name];
}

Material *VulkanEngine::getMaterial(const std::string &name) {
    auto it = m_materials.find(name);
    if (it == m_materials.end()) {
        return nullptr;
    }
    return &(it->second);
}

Mesh *VulkanEngine::getMesh(const std::string &name) {
    auto it = m_meshes.find(name);
    if (it == m_meshes.end()) {
        return nullptr;
    }
    return &(it->second);
}

FrameData &VulkanEngine::getCurrentFrame() {
    return m_frames[m_frameNumber % FRAMES_IN_FLIGHT];
}

AllocatedBuffer VulkanEngine::createBuffer(size_t size, vk::BufferUsageFlags usageFlags, vma::MemoryUsage memoryUsage) {
    vk::BufferCreateInfo info = {};
    info.size = size;
    info.usage = usageFlags;

    vma::AllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    AllocatedBuffer buffer;
    auto pair = m_allocator.createBuffer(info, allocInfo);
    buffer.buffer = pair.first;
    buffer.allocation = pair.second;

    return buffer;
}

vk::Pipeline PipelineBuilder::buildPipeline(vk::Device device, vk::RenderPass pass) {
    //Create viewportstate from the stored viewport and scissor.
    //At the moment we don't support multiple viewports or scissors.
    vk::PipelineViewportStateCreateInfo viewportInfo = {};
    viewportInfo.viewportCount = 1;
    viewportInfo.setViewports(m_viewport);
    viewportInfo.scissorCount = 1;
    viewportInfo.setScissors(m_scissor);

    //Set up dummy color blending. As we aren't yet using transparent objects, we don't do blending,
    //but we do write to the color attachment.
    vk::PipelineColorBlendStateCreateInfo colorBlendInfo = {};
    colorBlendInfo.logicOpEnable = VK_FALSE;
    colorBlendInfo.logicOp = vk::LogicOp::eCopy;
    colorBlendInfo.setAttachments(m_colorBlendAttachmentState);

    //The actual pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stageCount = m_shaderStageInfos.size();
    pipelineInfo.setStages(m_shaderStageInfos);
    pipelineInfo.pVertexInputState = &m_vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &m_inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pRasterizationState = &m_rasterizerInfo;
    pipelineInfo.pMultisampleState = &m_multisampleInfo;
    pipelineInfo.pColorBlendState = &colorBlendInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.pDepthStencilState = &m_depthStencil;

    auto pipeline = device.createGraphicsPipeline(VK_NULL_HANDLE, pipelineInfo);
    switch (pipeline.result) {
        case vk::Result::eSuccess:
            return pipeline.value;
        default:
            std::cout << "Pipeline create failed." << std::endl;
            return VK_NULL_HANDLE;
    }

    return vk::Pipeline();
}
