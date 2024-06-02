#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <glm/gtx/transform.hpp>
#include <stb_image.h>

#include <iostream>
#include <chrono>
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

    loadTextures();

    initScene();

    m_isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (m_isInitialized) {
        //Wait for the device to finish rendering before cleaning up
        m_vkDevice.waitIdle();

        //Delete terrain
        deleteAllTerrainChunks();

        //Destroy all objects in the deletion queues
        getCurrentFrame().frameDeletionQueue.flush();
        m_sceneDeletionQueue.flush();
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

    float timeDelta = 0.0f;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    int mouse_x = 0;
    int mouse_y = 0;
    while (!bQuit) {
        auto start = std::chrono::high_resolution_clock::now();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                bQuit = true;
            }
            else if (e.type == SDL_KEYDOWN) {
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
            else if (e.type == SDL_MOUSEMOTION) {
                if (e.motion.windowID == SDL_GetWindowID(m_sdlWindow)) {
                    m_camera.processMouseMovement((float)e.motion.xrel, (float)e.motion.yrel);
                }
            }
        }
        m_camera.processKeyboard(timeDelta);

        draw();
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::duration<float>>(end - start).count();
        timeDelta = elapsedTime;
        m_simulationTime += elapsedTime;
    }
}

void VulkanEngine::draw() {
    FrameData& frame = getCurrentFrame();

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
        vk::detail::throwResultException(nextImageResult, "Failed to acquire swap chain image.");
    }

    //Clear the frame's deletion queue
    frame.frameDeletionQueue.flush();

    updateTerrainChunks(frame.frameDeletionQueue);

    m_vkDevice.resetFences(frame.inFlightFence);

    //Reset the command buffer now that commands are done executing.
    auto & cmd = frame.mainCommandBuffer;
    cmd.reset();

    //Tell Vulkan we will only use this once
    vk::CommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmd.begin(cmdBeginInfo);

    //Clear screen to black
    vk::ClearValue clearValue = {};
    const std::array<float, 4> cols = {0.0f, 0.0f, 0.0f, 1.0f};
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

    //Concatenate renderables with terrain renderables
    std::vector<RenderObject> allRenderables;
    allRenderables.insert(allRenderables.end(), m_renderables.begin(), m_renderables.end());
    for (const auto& pair : m_terrainRenderables) {
        allRenderables.push_back(pair.second);
    }
    drawObjects(cmd, allRenderables.data(), allRenderables.size());


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
    }
    else if (presentResult != vk::Result::eSuccess) {
        vk::detail::throwResultException(nextImageResult, "Failed to present swap chain image.");
    }

    m_frameNumber++;
}

void VulkanEngine::drawObjects(vk::CommandBuffer cmd, RenderObject *first, int count) {
//    glm::vec3 camPos = {0.0f, 0.0f, -10.0f};
//    glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
//    float aspect = static_cast<float>(m_windowExtent.width) / static_cast<float>(m_windowExtent.height);
//    glm::mat4 projection = glm::perspective(glm::radians(70.0f), aspect, 0.1f, 200.0f);
//    projection[1][1] *= -1;

    float aspect = static_cast<float>(m_windowExtent.width) / static_cast<float>(m_windowExtent.height);
    glm::mat4 projection = glm::perspective(glm::radians(m_camera.m_fov), aspect, 0.1f, 200.0f);
    projection[1][1] *= -1;
    glm::mat4 view = m_camera.getViewMatrix();

    auto curFrame = getCurrentFrame();
    float uTime = m_simulationTime;

    //Fill the camera data struct...
    GPUCameraData camData = {};
    camData.view = view;
    camData.projection = projection;
    camData.viewProjection = projection * view;

    //...and copy it into the buffer
    GPUCameraData * data = static_cast<GPUCameraData *>(m_allocator.mapMemory(curFrame.cameraBuffer.allocation));
    memcpy(data, &camData, sizeof(GPUCameraData));
    m_allocator.unmapMemory(curFrame.cameraBuffer.allocation);

    //
    //Lights etc.
    //
    m_sceneParameters.ambientColor = {0.05f, 0.05f, 0.05f, 1.0f};

    //Sunlight data
    m_sceneParameters.sunlightColor = {0.3f, 0.2f, 0.1f, 32.0f};
    m_sceneParameters.sunlightDirection = {0.5f, 1.0f, 0.0f, 1.0f};

    //Copy scene parameters into GPU memory
    char * sceneData = static_cast<char *>(m_allocator.mapMemory(m_sceneParameterBuffer.allocation));
    uint64_t frameIdx = m_frameNumber % FRAMES_IN_FLIGHT;
    uint32_t uniformOffset = padUniformBufferSize(sizeof(GPUSceneData)) * frameIdx;
    sceneData += uniformOffset;
    memcpy(sceneData, &m_sceneParameters, sizeof(GPUSceneData));
    m_allocator.unmapMemory(m_sceneParameterBuffer.allocation);

    //Create point lights and copy them into GPU memory
//    float posMod = (sin(uTime) + 1.0f) / 2.0f * 10.0f;
//    std::array<PointLightData, 3> pointLights = {};
//    pointLights[0] = {{-3.0f - posMod/2, 3.0f, -0.0f - posMod, 32.0f}, {10.0f, 0.0f, 0.0f, 32.0f}};
//    pointLights[1] = {{-2.0f, 3.0f, -0.0f - posMod, 32.0f}, {0.0f, 10.0f, 0.0f, 32.0f}};
//    pointLights[2] = {{-1.0f + posMod/2, 3.0f, -0.0f - posMod, 32.0f}, {0.0f, 0.0f, 10.0f, 32.0f}};
//    PointLightData* pointLightSSBO = static_cast<PointLightData *>(m_allocator.mapMemory(curFrame.lightBuffer.allocation));
//    std::copy(pointLights.begin(), pointLights.end(), pointLightSSBO);
//    m_allocator.unmapMemory(curFrame.lightBuffer.allocation);

    //Copy object matrices into storage buffer
    GPUObjectData* objectSSBO = static_cast<GPUObjectData *>(m_allocator.mapMemory(curFrame.objectBuffer.allocation)); //unmapped after the object loop

    Mesh* lastMesh = nullptr;
    Material* lastMaterial = nullptr;

    //TODO: sort array by pipeline pointer to reduce number of binds, maybe?
    for (int i = 0; i < count; i++) {
        RenderObject& object = first[i];
        objectSSBO[i].modelMatrix = object.transformMatrix;

        //Only bind the pipeline if it doesn't match the already bound one
        if (object.material != lastMaterial) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, object.material->pipeline);
            lastMaterial = object.material;
            //Bind the camera data descriptor set when changing pipeline
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.material->pipelineLayout, 0, curFrame.globalDescriptor, uniformOffset);
            //Bind the object descriptor set too
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.material->pipelineLayout, 1, curFrame.objectDescriptor, nullptr);

            //Bind the texture descriptor set, if relevant
            if (object.material->textureSet.has_value()) {
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, object.material->pipelineLayout, 2, object.material->textureSet.value(), nullptr);
            }

            //Update viewport and scissor
            vk::Viewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_swapChainExtent.width);
            viewport.height = static_cast<float>(m_swapChainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            cmd.setViewport(0, viewport);

            vk::Rect2D scissor;
            scissor.offset = vk::Offset2D{0, 0};
            scissor.extent = m_swapChainExtent;
            cmd.setScissor(0, scissor);
        }

        MeshPushConstants constants;
        constants.renderMatrix = object.transformMatrix;
        cmd.pushConstants(object.material->pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(MeshPushConstants), &constants);

        //Push texIdx if the material is textured
        if (object.material->textureSet.has_value()) {
            int texIdx = static_cast<int>(object.textureId);
            cmd.pushConstants(object.material->pipelineLayout, vk::ShaderStageFlagBits::eFragment, sizeof(MeshPushConstants), sizeof(int), &texIdx);
        }

        //Only bind the mesh if it doesn't match the already bound one
        if (object.mesh != lastMesh) {
            vk::DeviceSize offset = 0;
            cmd.bindVertexBuffers(0, 1, &object.mesh->vertexBuffer.buffer, &offset);
            if (!object.mesh->indices.empty()) {
                cmd.bindIndexBuffer(object.mesh->indexBuffer.buffer, 0, vk::IndexType::eUint16);
            }
            lastMesh = object.mesh;
        }

        if (object.mesh->indices.empty()) {
            cmd.draw(object.mesh->vertices.size(), 1, 0,
                     i); //FIXME: we're hackily using the firstInstance parameter here to pass instance index to the shader, and I do not like it.
        }
        else {
            cmd.drawIndexed(object.mesh->indices.size(), 1, 0, 0, i);
        }
    }

    m_allocator.unmapMemory(curFrame.objectBuffer.allocation);
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

void VulkanEngine::createLogicalDevice() {
    //
    // Create logical device (vk::Device)
    //
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
    vk::PhysicalDeviceFeatures2 deviceFeatures = {};
    deviceFeatures.features.geometryShader = VK_TRUE;
    vk::PhysicalDeviceVulkan11Features vk11Features = {};
    deviceFeatures.pNext = &vk11Features;
    vk11Features.shaderDrawParameters = VK_TRUE;

    //Actually create the logical device
    vk::DeviceCreateInfo createInfo = {};
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pNext = &deviceFeatures;

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
    {
        vk::Extent3D depthImageExtent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
        m_depthFormat = vk::Format::eD32Sfloat;

        vk::ImageCreateInfo depthImgInfo = vkinit::imageCreateInfo(m_depthFormat,
                                                                   vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                                   depthImageExtent, m_msaaSamples);
        //We want the depth image in GPU local memory
        vma::AllocationCreateInfo depthAllocInfo = {};
        depthAllocInfo.usage = vma::MemoryUsage::eGpuOnly;
        depthAllocInfo.requiredFlags = vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal);

        //Allocate and create the depth buffer image
        auto imagePair = m_allocator.createImage(depthImgInfo, depthAllocInfo);
        m_depthImage.image = imagePair.first;
        m_depthImage.allocation = imagePair.second;

        vk::ImageViewCreateInfo depthViewInfo = vkinit::imageViewCreateInfo(m_depthFormat, m_depthImage.image,
                                                                            vk::ImageAspectFlagBits::eDepth);
        m_depthImageView = m_vkDevice.createImageView(depthViewInfo);
    }

    //
    // Create color resources for MSAA
    //
    {
        m_colorFormat = m_swapChainImageFormat;
        vk::Extent3D colorImageExtent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
        vk::ImageCreateInfo colorImgInfo = vkinit::imageCreateInfo(m_swapChainImageFormat,
                                                                   vk::ImageUsageFlagBits::eTransientAttachment |
                                                                   vk::ImageUsageFlagBits::eColorAttachment,
                                                                   colorImageExtent, m_msaaSamples);

        vma::AllocationCreateInfo colorAllocInfo = {};
        colorAllocInfo.usage = vma::MemoryUsage::eGpuOnly;
        colorAllocInfo.requiredFlags = vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal);

        auto imagePair = m_allocator.createImage(colorImgInfo, colorAllocInfo);
        m_colorImage.image = imagePair.first;
        m_colorImage.allocation = imagePair.second;

        vk::ImageViewCreateInfo colorViewInfo = vkinit::imageViewCreateInfo(m_colorFormat, m_colorImage.image, vk::ImageAspectFlagBits::eColor);
        m_colorImageView = m_vkDevice.createImageView(colorViewInfo);
    }
}

void VulkanEngine::createCommandPoolAndBuffers() {
    // Create a command pool and primary command buffer for each frame in flight

    vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    const auto graphicsFamily = findQueueFamilies(m_activeGPU).graphicsFamily.value();
    auto createInfo = vkinit::commandPoolCreateInfo(graphicsFamily, flags);

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

    //Create a command pool and buffer for the upload context
    vk::CommandPoolCreateInfo uploadPoolInfo = vkinit::commandPoolCreateInfo(graphicsFamily, vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
    m_uploadContext.commandPool = m_vkDevice.createCommandPool(uploadPoolInfo);
    m_mainDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyCommandPool(m_uploadContext.commandPool);
    });
    vk::CommandBufferAllocateInfo uploadPoolAllocInfo = {};
    uploadPoolAllocInfo.commandPool = m_uploadContext.commandPool;
    uploadPoolAllocInfo.commandBufferCount = 1;
    uploadPoolAllocInfo.level = vk::CommandBufferLevel::ePrimary;
    m_uploadContext.commandBuffer = m_vkDevice.allocateCommandBuffers(uploadPoolAllocInfo)[0];

    std::cout << "Created command pool and command buffer." << std::endl;
}

void VulkanEngine::createDefaultRenderPass() {
    //Quoting vkguide.dev:
    //The image life will go something like this:
    //UNDEFINED -> RenderPass Begins -> Subpass 0 begins (Transition to Attachment Optimal) -> Subpass 0 renders -> Subpass 0 ends -> Renderpass Ends (Transitions to Present Source)

    vk::AttachmentDescription colorAttachment = {};
    colorAttachment.format = m_colorFormat;
    colorAttachment.samples = m_msaaSamples;
    //Clear when attachment is loaded
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    //Store when render pass ends
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    //Don't care about these (yet?)
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

    //Don't know or care about the starting layout
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;

    //After the render pass is through, the image is in color attachment format, that needs to be converted to presentation format
    colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::AttachmentReference colorAttachmentRef = {};
    //This will index into the pAttachments array in the parent render pass
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    //Create depth buffer attachment
    vk::AttachmentDescription depthAttachment = {};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = m_msaaSamples;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;


    //Resolve the color attachment (for MSAA)
    vk::AttachmentDescription colorAttachmentResolve = {};
    colorAttachmentResolve.format = m_swapChainImageFormat;
    colorAttachmentResolve.samples = vk::SampleCountFlagBits::e1; //presentable images must only have 1 sample
    colorAttachmentResolve.loadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachmentResolve.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachmentResolve.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachmentResolve.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachmentResolve.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachmentResolve.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentResolveRef = {};
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

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
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    //Finally, create the actual render pass
    vk::RenderPassCreateInfo createInfo = {};
    vk::SubpassDependency dependencies[2] = {colorDependency, depthDependency};
    vk::AttachmentDescription attachments[3] = {colorAttachment, depthAttachment, colorAttachmentResolve};
    createInfo.setAttachments(attachments);
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.setDependencies(dependencies);

    m_renderPass = m_vkDevice.createRenderPass(createInfo);
    m_mainDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyRenderPass(m_renderPass);
    });
}

void VulkanEngine::createFramebuffers() {
    vk::FramebufferCreateInfo fbInfo = {};
    fbInfo.pNext = nullptr;
    fbInfo.renderPass = m_renderPass;
    fbInfo.width = m_swapChainExtent.width;
    fbInfo.height = m_swapChainExtent.height;
    fbInfo.layers = 1;

    //Create a framebuffer for each swap chain image view
    for (const auto & view : m_swapChainImageViews) {
        vk::ImageView attachments[3] = {m_colorImageView, m_depthImageView, view};
        fbInfo.setAttachments(attachments);
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

    vk::FenceCreateInfo uploadFenceInfo = {};
    m_uploadContext.uploadFence = m_vkDevice.createFence(uploadFenceInfo);
    m_mainDeletionQueue.pushFunction([=] () {
        m_vkDevice.destroyFence(m_uploadContext.uploadFence);
    });
}

void VulkanEngine::createDescriptors() {
    //
    // Descriptor set layout 0
    //

    //Bind camera data at 0
    vk::DescriptorSetLayoutBinding camBinding = vkinit::descriptorSetLayoutBinding(vk::DescriptorType::eUniformBuffer,
                                                                                   vk::ShaderStageFlagBits::eVertex, 0,
                                                                                   1);

    //Bind scene parameters at 1
    vk::DescriptorSetLayoutBinding sceneBinding = vkinit::descriptorSetLayoutBinding(
            vk::DescriptorType::eUniformBufferDynamic,
            vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, 1, 1);

    vk::DescriptorSetLayoutBinding bindings0[] = {camBinding, sceneBinding};

    vk::DescriptorSetLayoutCreateInfo set0Info = {};
    set0Info.setBindings(bindings0);

    m_globalDescriptorSetLayout = m_vkDevice.createDescriptorSetLayout(set0Info);
    m_mainDeletionQueue.pushFunction([=] () {
        m_vkDevice.destroyDescriptorSetLayout(m_globalDescriptorSetLayout);
    });

    //
    // Descriptor set layout 1
    //
    //Bind objects at 0
    vk::DescriptorSetLayoutBinding objBinding = vkinit::descriptorSetLayoutBinding(vk::DescriptorType::eStorageBuffer,
                                                                                   vk::ShaderStageFlagBits::eVertex, 0,
                                                                                   1);

    //Bind lights at 1
    vk::DescriptorSetLayoutBinding lightBinding = vkinit::descriptorSetLayoutBinding(vk::DescriptorType::eStorageBuffer,
                                                                                     vk::ShaderStageFlagBits::eFragment,
                                                                                     1, 1);
    vk::DescriptorSetLayoutBinding bindings1[] = {objBinding, lightBinding};
    vk::DescriptorSetLayoutCreateInfo set1Info = {};
    set1Info.setBindings(bindings1);

    m_objectDescriptorSetLayout = m_vkDevice.createDescriptorSetLayout(set1Info);
    m_mainDeletionQueue.pushFunction([=] () {
        m_vkDevice.destroyDescriptorSetLayout(m_objectDescriptorSetLayout);
    });


    //
    // Descriptor set layout 2. We don't allocate it here yet.
    //
    vk::DescriptorSetLayoutBinding texBinding = vkinit::descriptorSetLayoutBinding(
            vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0, TEXTURE_ARRAY_SIZE);

    vk::DescriptorSetLayoutCreateInfo set2Info = {};
    set2Info.setBindings(texBinding);
    m_textureDescriptorSetLayout = m_vkDevice.createDescriptorSetLayout(set2Info);
    m_mainDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyDescriptorSetLayout(m_textureDescriptorSetLayout);
    });

    //Third one for terrain textures
    vk::DescriptorSetLayoutBinding terrainTexBinding = vkinit::descriptorSetLayoutBinding(
            vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0, TERRAIN_TEXTURE_ARRAY_SIZE);
    vk::DescriptorSetLayoutCreateInfo set3Info = {};
    set3Info.setBindings(terrainTexBinding);
    m_terrainTextureDescriptorSetLayout = m_vkDevice.createDescriptorSetLayout(set3Info);
    m_mainDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyDescriptorSetLayout(m_terrainTextureDescriptorSetLayout);
    });

    //Create a descriptor pool to hold 10 uniform buffers, and 10 dynamic uniform buffers
    std::vector<vk::DescriptorPoolSize> sizes = {
            { vk::DescriptorType::eUniformBuffer, 10 },
            { vk::DescriptorType::eUniformBufferDynamic, 10 },
            { vk::DescriptorType::eStorageBuffer, 10 },
            { vk::DescriptorType::eCombinedImageSampler, 10 }
    };

    vk::DescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.maxSets = 10;
    poolCreateInfo.setPoolSizes(sizes);

    m_descriptorPool = m_vkDevice.createDescriptorPool(poolCreateInfo);
    m_mainDeletionQueue.pushFunction([=] () {
        m_vkDevice.destroyDescriptorPool(m_descriptorPool);
    });

    //Create buffer for scene parameters
    size_t sceneParameterBufferSize = FRAMES_IN_FLIGHT * padUniformBufferSize(sizeof(GPUSceneData));
    m_sceneParameterBuffer = createBuffer(sceneParameterBufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vma::MemoryUsage::eCpuToGpu);
    m_mainDeletionQueue.pushFunction([=] () {
        destroyBuffer(m_sceneParameterBuffer);
    });

    //Create per-frame buffers
    for (int i = 0; i < std::size(m_frames); i++) {
        auto & frame = m_frames[i];

        const int MAX_OBJECTS = 10000;
        frame.objectBuffer = createBuffer(sizeof(GPUObjectData) * MAX_OBJECTS, vk::BufferUsageFlagBits::eStorageBuffer, vma::MemoryUsage::eCpuToGpu);

        frame.cameraBuffer = createBuffer(sizeof(GPUCameraData), vk::BufferUsageFlagBits::eUniformBuffer, vma::MemoryUsage::eCpuToGpu);

        const int MAX_LIGHTS = 10;
        frame.lightBuffer = createBuffer(sizeof(PointLightData) * MAX_LIGHTS, vk::BufferUsageFlagBits::eStorageBuffer, vma::MemoryUsage::eCpuToGpu);

        m_mainDeletionQueue.pushFunction([=] () {
            destroyBuffer(frame.objectBuffer);
            destroyBuffer(frame.cameraBuffer);
            destroyBuffer(frame.lightBuffer);
        });

        //Allocate one descriptor set for each frame
        vk::DescriptorSetAllocateInfo allocateInfo = {};
        allocateInfo.descriptorPool = m_descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.setSetLayouts(m_globalDescriptorSetLayout);

        frame.globalDescriptor = m_vkDevice.allocateDescriptorSets(allocateInfo)[0]; //We only get one result from this call

        //Point the descriptor to the camera buffer
        vk::DescriptorBufferInfo cameraInfo = {};
        cameraInfo.buffer = frame.cameraBuffer.buffer;
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(GPUCameraData);

        //Point the descriptor to the scene parameter buffer
        vk::DescriptorBufferInfo sceneInfo = {};
        sceneInfo.buffer = m_sceneParameterBuffer.buffer;
        sceneInfo.offset = 0; //this is a dynamic buffer, therefore 0
        sceneInfo.range = sizeof(GPUSceneData);

        //Allocate object descriptor set
        vk::DescriptorSetAllocateInfo objAllocateInfo = {};
        objAllocateInfo.descriptorPool = m_descriptorPool;
        objAllocateInfo.descriptorSetCount = 1;
        objAllocateInfo.setSetLayouts(m_objectDescriptorSetLayout);

        frame.objectDescriptor = m_vkDevice.allocateDescriptorSets(objAllocateInfo)[0];

        //Point the object descriptor to the object buffer
        vk::DescriptorBufferInfo objectInfo = {};
        objectInfo.buffer = frame.objectBuffer.buffer;
        objectInfo.offset = 0;
        objectInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

        //Point the light descriptor to the light buffer
        vk::DescriptorBufferInfo lightInfo = {};
        lightInfo.buffer = frame.lightBuffer.buffer;
        lightInfo.offset = 0;
        lightInfo.range = sizeof(PointLightData) * MAX_LIGHTS;

        vk::WriteDescriptorSet cameraWrite = vkinit::writeDescriptorSet(vk::DescriptorType::eUniformBuffer, frame.globalDescriptor, &cameraInfo, 0);
        vk::WriteDescriptorSet sceneWrite = vkinit::writeDescriptorSet(vk::DescriptorType::eUniformBufferDynamic, frame.globalDescriptor, &sceneInfo, 1);
        vk::WriteDescriptorSet objectWrite = vkinit::writeDescriptorSet(vk::DescriptorType::eStorageBuffer, frame.objectDescriptor, &objectInfo, 0);
        vk::WriteDescriptorSet lightWrite = vkinit::writeDescriptorSet(vk::DescriptorType::eStorageBuffer, frame.objectDescriptor, &lightInfo, 1);
        vk::WriteDescriptorSet setWrite[] = {cameraWrite, sceneWrite, objectWrite, lightWrite};

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

    //Pick max usable MSAA sample count
    auto properties = m_activeGPU.getProperties();
    vk::SampleCountFlags counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e16) { m_msaaSamples = vk::SampleCountFlagBits::e16; }
    else if (counts & vk::SampleCountFlagBits::e8) { m_msaaSamples = vk::SampleCountFlagBits::e8; }
    else if (counts & vk::SampleCountFlagBits::e4) { m_msaaSamples = vk::SampleCountFlagBits::e4; }
    else if (counts & vk::SampleCountFlagBits::e2) { m_msaaSamples = vk::SampleCountFlagBits::e2; }
    else { m_msaaSamples = vk::SampleCountFlagBits::e1; }
    std::cout << "Using " << to_string(m_msaaSamples) << "x MSAA" << std::endl;

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

    m_gpuProperties = m_activeGPU.getProperties();
    std::cout << "GPU minimum buffer alignment: " << m_gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;
}

void VulkanEngine::initScene() {
//    RenderObject terrain = {};
//    terrain.mesh = getMesh("heightmap");
//    terrain.material = getMaterial("terrain");
//    terrain.transformMatrix = glm::translate(glm::vec3{0, 0, -5});
//    terrain.textureId = 0;
//    m_renderables.push_back(terrain);

//    RenderObject monkey;
//    monkey.mesh = getMesh("monkey");
//    monkey.material = getMaterial("defaultmesh");
//    //monkey.transformMatrix = glm::mat4{1.0f};
//    monkey.transformMatrix = glm::translate(glm::vec3{0, 10, -5});
//    m_renderables.push_back(monkey);

    auto mesh = getMesh("monkey");
    auto mat = getMaterial("texturedmesh");
    for (size_t i = 0; i < TEXTURE_ARRAY_SIZE; i++) {
        RenderObject monke;
        monke.mesh = mesh;
        monke.material = mat;
        monke.transformMatrix = glm::translate(glm::vec3(-6.0f + i*3, 0, 0));
        monke.textureId = i;
        m_renderables.push_back(monke);
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

    vk::PhysicalDeviceFeatures2 physicalDeviceFeatures2 = {};
    vk::PhysicalDeviceVulkan11Features vk11Features = {};
    physicalDeviceFeatures2.pNext = &vk11Features;
    vk::PhysicalDeviceVulkan12Features vk12Features = {};
    vk11Features.pNext = &vk12Features;
    vk::PhysicalDeviceVulkan13Features vk13Features = {};
    vk12Features.pNext = &vk13Features;

    device.getFeatures2(&physicalDeviceFeatures2);
    auto vk10Features = physicalDeviceFeatures2.features;
    int score = 0;

    //The device must have a geometry shader to be useful
    if (!vk10Features.geometryShader) {
        return 0;
    }
    //We also need shader draw parameters
    //TODO: refactor required features so that they're only in one place, rather than duplicated in here and in createLogicalDevice()
    if (!vk11Features.shaderDrawParameters) {
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
//TODO: actually just refactor this whole function, JFC.
void VulkanEngine::createPipelines() {
    //Default placeholder shader
    vk::ShaderModule defaultLitFragShader = loadShaderModule("shaders/default_lit.frag.spv");
    //Textured shader
    vk::ShaderModule defaultTexFragShader = loadShaderModule("shaders/textured_lit.frag.spv");
    //Terrain
    vk::ShaderModule defaultTerrainFragShader = loadShaderModule("shaders/terrain_textured_lit.frag.spv");
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

    //yes multisampling
    pipelineBuilder.m_multisampleInfo = vkinit::multisampleStateCreateInfo(m_msaaSamples);

    //a single blend attachment with no blending, writing to RGBA
    pipelineBuilder.m_colorBlendAttachmentState = vkinit::pipelineColorBlendAttachmentState();

    //
    //Build the mesh pipeline
    //
    VertexInputDescription vertexDescription = Vertex::getVertexDescription();

    vk::PipelineLayoutCreateInfo meshPipelineInfo = vkinit::pipelineLayoutCreateInfo();

    //Set up push constants
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MeshPushConstants);
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
    meshPipelineInfo.setPushConstantRanges(pushConstantRange);

    //Hook up the set layouts
    vk::DescriptorSetLayout setLayouts[] = {m_globalDescriptorSetLayout, m_objectDescriptorSetLayout};
    meshPipelineInfo.setSetLayouts(setLayouts);

    m_meshPipelineLayout = m_vkDevice.createPipelineLayout(meshPipelineInfo); //queued for deletion at the bottom of this func

    //Connect the builder vertex input info to the one we got from Vertex::
    pipelineBuilder.m_vertexInputInfo.setVertexAttributeDescriptions(vertexDescription.attributes);
    pipelineBuilder.m_vertexInputInfo.setVertexBindingDescriptions(vertexDescription.bindings);

    //Add shaders
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, meshVertShader));
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, defaultLitFragShader));

    //Hook the new layout into the pipelineBuilder
    pipelineBuilder.m_pipelineLayout = m_meshPipelineLayout;

    //Default depth testing
    pipelineBuilder.m_depthStencil = vkinit::depthStencilStateCreateInfo(true, true, vk::CompareOp::eLessOrEqual);

    //Finally, build the pipeline
    m_meshPipeline = pipelineBuilder.buildPipeline(m_vkDevice, m_renderPass);

    //Add the pipeline to our materials
    createMaterial(m_meshPipeline, m_meshPipelineLayout, "defaultmesh");

    //
    //Build a pipeline for textured mesh
    //
    vk::PipelineLayoutCreateInfo texPipelineInfo = meshPipelineInfo;

    //Create extra push constant for this pipeline for fragment shaders
    vk::PushConstantRange texPushConstants[2];
    texPushConstants[0] = pushConstantRange;
    texPushConstants[1].offset = sizeof(MeshPushConstants);
    texPushConstants[1].size = sizeof(int);
    texPushConstants[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
    texPipelineInfo.setPushConstantRanges(texPushConstants);

    vk::DescriptorSetLayout texSetLayouts[] = {m_globalDescriptorSetLayout, m_objectDescriptorSetLayout, m_textureDescriptorSetLayout};
    texPipelineInfo.setSetLayouts(texSetLayouts);

    auto texPipelineLayout = m_vkDevice.createPipelineLayout(texPipelineInfo);
    pipelineBuilder.m_shaderStageInfos.clear();
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, meshVertShader));
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, defaultTexFragShader));
    pipelineBuilder.m_pipelineLayout = texPipelineLayout;
    auto texPipeline = pipelineBuilder.buildPipeline(m_vkDevice, m_renderPass);
    createMaterial(texPipeline, texPipelineLayout, "texturedmesh");

    //Textured terrain pipeline, similar to the above one for textured meshes
    vk::PipelineLayoutCreateInfo terrainPipelineInfo = meshPipelineInfo;

    vk::PushConstantRange texTerrainPushConstants[2];
    texTerrainPushConstants[0] = pushConstantRange;
    texTerrainPushConstants[1].offset = sizeof(MeshPushConstants);
    texTerrainPushConstants[1].size = sizeof(int);
    texTerrainPushConstants[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
    terrainPipelineInfo.setPushConstantRanges(texTerrainPushConstants);

    vk::DescriptorSetLayout terrainSetLayouts[] = {m_globalDescriptorSetLayout, m_objectDescriptorSetLayout, m_terrainTextureDescriptorSetLayout};
    terrainPipelineInfo.setSetLayouts(terrainSetLayouts);

    auto terrainPipelineLayout = m_vkDevice.createPipelineLayout(terrainPipelineInfo);
    pipelineBuilder.m_shaderStageInfos.clear();
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eVertex, meshVertShader));
    pipelineBuilder.m_shaderStageInfos.push_back(vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits::eFragment, defaultTerrainFragShader));
    pipelineBuilder.m_pipelineLayout = terrainPipelineLayout;
    auto terrainPipeline = pipelineBuilder.buildPipeline(m_vkDevice, m_renderPass);
    createMaterial(terrainPipeline, terrainPipelineLayout, "terrain");

    //Destroy shader modules
    m_vkDevice.destroyShaderModule(meshVertShader);
    m_vkDevice.destroyShaderModule(defaultLitFragShader);
    m_vkDevice.destroyShaderModule(defaultTexFragShader);
    m_vkDevice.destroyShaderModule(defaultTerrainFragShader);

    //Queue destruction of pipelines
    m_pipelineDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyPipeline(m_meshPipeline);
        m_vkDevice.destroyPipelineLayout(m_meshPipelineLayout);

        m_vkDevice.destroyPipeline(texPipeline);
        m_vkDevice.destroyPipelineLayout(texPipelineLayout);

        m_vkDevice.destroyPipeline(terrainPipeline);
        m_vkDevice.destroyPipelineLayout(terrainPipelineLayout);
    });
}

void VulkanEngine::loadMeshes() {
    //Monke mesh
    Mesh monke;
    monke.loadFromObj("data/assets/monkey_smooth.obj");
    uploadMesh(monke);
    m_meshes["monkey"] = monke;
//
//    //Minecraft mesh
//    Mesh mine;
//    mine.loadFromObj("data/assets/lost_empire.obj");
//    uploadMesh(mine);
//    m_meshes["mine"] = mine;

    //Heightmap
    Mesh heightmap;
    heightmap.loadFromHeightmap("data/assets/test_heightmap.png");
    uploadMesh(heightmap);
    m_meshes["heightmap"] = heightmap;

    std::cout << "Loaded meshes." << std::endl;
}

//Uploads a mesh to a GPU local buffer
void VulkanEngine::uploadMesh(Mesh &mesh, bool addToDeletionQueue) {
    //Allocate a CPU side staging buffer to hold mesh before uploading
    const size_t bufferSize = mesh.vertices.size() * sizeof(Vertex);
    AllocatedBuffer stagingBuffer = createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vma::MemoryUsage::eCpuOnly);

    //Copy vertex data into this buffer
    Vertex * vertexData = static_cast<Vertex *>(m_allocator.mapMemory(stagingBuffer.allocation));
    std::copy(mesh.vertices.begin(), mesh.vertices.end(), vertexData);
    m_allocator.unmapMemory(stagingBuffer.allocation);

    //Allocate GPU side vertex buffer that actually holds the mesh in VRAM
    mesh.vertexBuffer = createBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vma::MemoryUsage::eGpuOnly);

    //Submit copy command
    submitImmediateCommand([=](vk::CommandBuffer cmd) {
        vk::BufferCopy copy = {};
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = bufferSize;
        cmd.copyBuffer(stagingBuffer.buffer, mesh.vertexBuffer.buffer, copy);
    });

    //Clean up
    if (addToDeletionQueue) {
        m_mainDeletionQueue.pushFunction([=]() {
            destroyBuffer(mesh.vertexBuffer);
        });
    }
    destroyBuffer(stagingBuffer);

    //Do the same for the index buffer
    if (!mesh.indices.empty()) {
        const size_t indexBufferSize = mesh.indices.size() * sizeof(uint16_t);
        AllocatedBuffer indexStagingBuffer = createBuffer(indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vma::MemoryUsage::eCpuOnly);

        uint16_t * indexData = static_cast<uint16_t *>(m_allocator.mapMemory(indexStagingBuffer.allocation));
        std::copy(mesh.indices.begin(), mesh.indices.end(), indexData);
        m_allocator.unmapMemory(indexStagingBuffer.allocation);
        mesh.indexBuffer = createBuffer(indexBufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vma::MemoryUsage::eGpuOnly);
        submitImmediateCommand([=](vk::CommandBuffer cmd) {
            vk::BufferCopy copy = {};
            copy.dstOffset = 0;
            copy.srcOffset = 0;
            copy.size = indexBufferSize;
            cmd.copyBuffer(indexStagingBuffer.buffer, mesh.indexBuffer.buffer, copy);
        });
        if (addToDeletionQueue) {
            m_mainDeletionQueue.pushFunction([=]() {
                destroyBuffer(mesh.indexBuffer);
            });
        }
        destroyBuffer(indexStagingBuffer);
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
    m_vkDevice.destroyImageView(m_colorImageView);
    m_allocator.destroyImage(m_colorImage.image, m_colorImage.allocation);

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

void VulkanEngine::destroyBuffer(AllocatedBuffer buffer) {
    m_allocator.destroyBuffer(buffer.buffer, buffer.allocation);
}

//Pads a given size to align with the minimum uniform buffer offset alignment value.
size_t VulkanEngine::padUniformBufferSize(size_t originalSize) {
    size_t minUboAlignment = m_gpuProperties.limits.minUniformBufferOffsetAlignment;
    size_t alignedSize = originalSize;
    if (minUboAlignment > 0) {
        alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
    }
    return alignedSize;
}

void VulkanEngine::submitImmediateCommand(std::function<void(vk::CommandBuffer)> &&function) {
    auto cmd = m_uploadContext.commandBuffer;

    //Begin recording. We use this buffer only once, so give Vulkan a hint about that
    vk::CommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    cmd.begin(cmdBeginInfo);
    //Execute the immediate command function
    function(cmd);
    cmd.end();

    vk::SubmitInfo submitInfo = {};
    submitInfo.setCommandBuffers(cmd);

    //Submit the command buffer to the queue and execute it.
    //uploadFence will now block until the graphic commands finish execution
    m_graphicsQueue.submit(submitInfo, m_uploadContext.uploadFence);
    auto res = m_vkDevice.waitForFences(m_uploadContext.uploadFence, true, S_TO_NS(5)); //FIXME: handle timeout
    m_vkDevice.resetFences(m_uploadContext.uploadFence);
    m_vkDevice.resetCommandPool(m_uploadContext.commandPool);
}

AllocatedImage VulkanEngine::loadImageFromFile(const char *filename) {
    int texWidth, texHeight, texChannels;

    //Load  image from file
    unsigned char * pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        std::cout << "Failed to load texture from file " << filename << std::endl;
        throw std::invalid_argument("Failed to load texture");
    }

    //Create staging buffer to hold the image
    vk::DeviceSize imgSize = texWidth * texHeight * 4; //4 bytes per pixel
    vk::Format imgFormat = vk::Format::eR8G8B8A8Srgb; //...and RGBA
    AllocatedBuffer stagingBuffer = createBuffer(imgSize, vk::BufferUsageFlagBits::eTransferSrc, vma::MemoryUsage::eCpuOnly);

    //Copy pixel data into staging buffer
    unsigned char * stagingData = static_cast<unsigned char *>(m_allocator.mapMemory(stagingBuffer.allocation));
    memcpy(stagingData, pixels, static_cast<size_t>(imgSize));
    m_allocator.unmapMemory(stagingBuffer.allocation);

    stbi_image_free(pixels);


    //Create the image
    vk::Extent3D imgExtent = {};
    imgExtent.width = static_cast<uint32_t>(texWidth);
    imgExtent.height = static_cast<uint32_t>(texHeight);
    imgExtent.depth = 1;

    vk::ImageCreateInfo imgCreateInfo = vkinit::imageCreateInfo(imgFormat, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, imgExtent);
    vma::AllocationCreateInfo imgAllocInfo = {};
    imgAllocInfo.usage = vma::MemoryUsage::eGpuOnly;
    auto imgPair = m_allocator.createImage(imgCreateInfo, imgAllocInfo);
    AllocatedImage image;
    image.image = imgPair.first;
    image.allocation = imgPair.second;

    //Copy the image
    submitImmediateCommand([=](vk::CommandBuffer cmd) {
        //First, transform the image, so it can be written to
        vk::ImageSubresourceRange range = {};
        range.aspectMask = vk::ImageAspectFlagBits::eColor;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        vk::ImageMemoryBarrier imgBarrier_toTransfer = {};
        imgBarrier_toTransfer.oldLayout = vk::ImageLayout::eUndefined;
        imgBarrier_toTransfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
        imgBarrier_toTransfer.image = image.image;
        imgBarrier_toTransfer.subresourceRange = range;
        imgBarrier_toTransfer.srcAccessMask = vk::AccessFlagBits::eNone;
        imgBarrier_toTransfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        //This barrier transitions the image into the transfer write layout
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, imgBarrier_toTransfer);

        //Next, transfer the image from the staging buffer into the image
        vk::BufferImageCopy copyRegion = {};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = imgExtent;

        cmd.copyBufferToImage(stagingBuffer.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

        //The image is now copied, so we need to transform it once more into a shader-readable layout
        vk::ImageMemoryBarrier imgBarrier_toReadable = imgBarrier_toTransfer;
        imgBarrier_toReadable.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        imgBarrier_toReadable.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imgBarrier_toReadable.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        imgBarrier_toReadable.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        //This barrier transitions the image into shader readable layout
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, imgBarrier_toReadable);
    });

    //Cleanup
    m_mainDeletionQueue.pushFunction([=]() {
        m_allocator.destroyImage(image.image, image.allocation);
    });
    destroyBuffer(stagingBuffer);

    std::cout << "Loaded texture " << filename << std::endl;

    return image;
}

Texture VulkanEngine::loadTexture(std::string file) {
    Texture tex;
    tex.image = loadImageFromFile(file.c_str());
    vk::ImageViewCreateInfo imgInfo = vkinit::imageViewCreateInfo(vk::Format::eR8G8B8A8Srgb, tex.image.image, vk::ImageAspectFlagBits::eColor);
    tex.imageView = m_vkDevice.createImageView(imgInfo);
    m_mainDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroyImageView(tex.imageView);
    });
    return tex;
}

void VulkanEngine::loadTextures() {
    m_textures.push_back(loadTexture("data/assets/brick.png"));
    m_textures.push_back(loadTexture("data/assets/concrete.png"));
    m_textures.push_back(loadTexture("data/assets/fabric.png"));
    m_textures.push_back(loadTexture("data/assets/rust.png"));
    m_textures.push_back(loadTexture("data/assets/wood.png"));

    vk::SamplerCreateInfo samplerInfo = vkinit::samplerCreateInfo(vk::Filter::eLinear);
    m_linearSampler = m_vkDevice.createSampler(samplerInfo);
    m_sceneDeletionQueue.pushFunction([=]() {
        m_vkDevice.destroySampler(m_linearSampler);
    });

    auto material = getMaterial("texturedmesh");
    vk::DescriptorSetAllocateInfo allocInfo = {};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.setSetLayouts(m_textureDescriptorSetLayout);
    m_textureDescriptorSet = m_vkDevice.allocateDescriptorSets(allocInfo)[0];
    material->textureSet = m_textureDescriptorSet;

    vk::DescriptorImageInfo imgInfos[TEXTURE_ARRAY_SIZE];
    for (size_t i = 0; i < TEXTURE_ARRAY_SIZE; i++) {
        auto& info = imgInfos[i];
        info.sampler = m_linearSampler;
        info.imageView = m_textures[i].imageView;
        info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
    vk::WriteDescriptorSet tex1 = vkinit::writeDescriptorSet(vk::DescriptorType::eCombinedImageSampler,
                                                             m_textureDescriptorSet, imgInfos, 0, TEXTURE_ARRAY_SIZE);
    m_vkDevice.updateDescriptorSets(tex1, nullptr);

    //Terrain textures use a different set
    m_terrainTextures.push_back(loadTexture("data/assets/grass.png"));
    m_terrainTextures.push_back(loadTexture("data/assets/rock.png"));
    m_terrainTextures.push_back(loadTexture("data/assets/snow.png"));

    auto terrainMaterial = getMaterial("terrain");
    vk::DescriptorSetAllocateInfo terrainAllocInfo = {};
    terrainAllocInfo.descriptorPool = m_descriptorPool;
    terrainAllocInfo.setSetLayouts(m_terrainTextureDescriptorSetLayout);
    m_terrainTextureDescriptorSet = m_vkDevice.allocateDescriptorSets(terrainAllocInfo)[0];
    terrainMaterial->textureSet = m_terrainTextureDescriptorSet;

    vk::DescriptorImageInfo terrainImgInfos[TERRAIN_TEXTURE_ARRAY_SIZE];
    for (size_t i = 0; i < TERRAIN_TEXTURE_ARRAY_SIZE; i++) {
        auto& info = terrainImgInfos[i];
        info.sampler = m_linearSampler;
        info.imageView = m_terrainTextures[i].imageView;
        info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
    vk::WriteDescriptorSet terrainTex1 = vkinit::writeDescriptorSet(vk::DescriptorType::eCombinedImageSampler,
                                                                    m_terrainTextureDescriptorSet, terrainImgInfos, 0, TERRAIN_TEXTURE_ARRAY_SIZE);
    m_vkDevice.updateDescriptorSets(terrainTex1, nullptr);

    std::cout << "Loaded textures." << std::endl;
}

void VulkanEngine::generateTerrainChunk(int x, int z) {
    Mesh mesh;
    mesh.sampleFromNoise(x, z, m_terrainChunkSize);
    uploadMesh(mesh, false);
    auto result = m_terrainMeshes.insert({std::make_pair(x, z), mesh});
    if (!result.second) {
        std::cout << "Failed to insert terrain mesh at " << x << ", " << z << std::endl;
        return;
    }
    Mesh * meshPtr = &(result.first->second);
    RenderObject terrain = {};
    terrain.mesh = meshPtr;
    terrain.material = getMaterial("terrain");
    terrain.transformMatrix = glm::translate(glm::vec3{x * (m_terrainChunkSize - 1), 0, z * (m_terrainChunkSize - 1)});
    terrain.textureId = 0;
    m_terrainRenderables[std::make_pair(x, z)] = terrain;

    std::cout << "Generated terrain chunk at " << x << ", " << z << std::endl;
}

void VulkanEngine::deleteTerrainChunk(int x, int z, DeletionQueue& deletionQueue) {
    auto pair = std::make_pair(x, z);
    m_terrainRenderables.erase(pair);

    auto it = m_terrainMeshes.find(pair);
    if (it != m_terrainMeshes.end()) {
        auto vertexBuffer = it->second.vertexBuffer;
        deletionQueue.pushFunction([=]() {
            destroyBuffer(vertexBuffer);
        });
        if (it->second.indexBuffer.buffer != VK_NULL_HANDLE) {
            auto indexBuffer = it->second.indexBuffer;
            deletionQueue.pushFunction([=]() {
                destroyBuffer(indexBuffer);
            });
        }
        m_terrainMeshes.erase(it);
    }

    std::cout << "Deleted terrain chunk at " << x << ", " << z << std::endl;
}

void VulkanEngine::updateTerrainChunks(DeletionQueue& deletionQueue) {
    auto camPos = m_camera.m_position;
    int camX = static_cast<int>(camPos.x / m_terrainChunkSize);
    int camZ = static_cast<int>(camPos.z / m_terrainChunkSize);

    //Delete chunks out of range
    std::vector<std::pair<int, int>> toDelete;
    for (auto & pair : m_terrainRenderables) {
        int x = pair.first.first;
        int z = pair.first.second;
        if (std::abs(x - camX) > m_terrainRenderDistance || std::abs(z - camZ) > m_terrainRenderDistance) {
            toDelete.push_back(pair.first);
        }
    }
    for (auto & pair : toDelete) {
        deleteTerrainChunk(pair.first, pair.second, deletionQueue);
    }

    //Generate new chunks in range
    for (int x = camX - m_terrainRenderDistance; x <= camX + m_terrainRenderDistance; x++) {
        for (int z = camZ - m_terrainRenderDistance; z <= camZ + m_terrainRenderDistance; z++) {
            auto pair = std::make_pair(x, z);
            if (m_terrainRenderables.find(pair) == m_terrainRenderables.end()) {
                generateTerrainChunk(x, z);
            }
        }
    }
}

void VulkanEngine::deleteAllTerrainChunks() {
    std::vector<std::pair<int, int>> toDelete;
    for (auto & pair : m_terrainRenderables) {
        toDelete.push_back(pair.first);
    }
    for (auto & pair : toDelete) {
        deleteTerrainChunk(pair.first, pair.second, m_mainDeletionQueue);
    }
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

    //Dynamic scissor and viewport
    vk::PipelineDynamicStateCreateInfo dynInfo = {};
    vk::DynamicState dynamicStates[] = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    dynInfo.setDynamicStates(dynamicStates);
    pipelineInfo.setPDynamicState(&dynInfo);

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
