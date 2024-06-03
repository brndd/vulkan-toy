#ifndef VKENG_VK_ENGINE_H
#define VKENG_VK_ENGINE_H


#include <optional>
#include <deque>
#include <glm/glm.hpp>
#include <chrono>
#include <functional>
#include <PerlinNoise.hpp>
#include "vk_types.h"
#include "vk_mesh.h"
#include "camera.h"

constexpr int FRAMES_IN_FLIGHT = 2;

constexpr size_t TEXTURE_ARRAY_SIZE = 5;
constexpr size_t TERRAIN_TEXTURE_ARRAY_SIZE = 3;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

/*
 * Simple (and somewhat inefficient) deletion queue for cleaning up Vulkan objects when the engine exists.
 * Heavily inspired by vkguide.dev (like everything else here)
 */
struct DeletionQueue {
    std::deque<std::function<void()>> deleters;

    void pushFunction(std::function<void()> && func) {
        deleters.push_back(func);
    }

    void flush() {
        for (auto it = deleters.rbegin(); it != deleters.rend(); it++) {
            (*it)();
        }
        deleters.clear();
    }
};

struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
};

struct Material {
    std::optional<vk::DescriptorSet> textureSet;
    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
};

struct RenderObject {
    Mesh * mesh;
    Material * material;
    size_t textureId;
    glm::mat4 transformMatrix;
};

//Holds view matrix, projection matrix and view*projection matrix.
struct GPUCameraData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
};

struct FrameData {
    vk::Semaphore imageAvailableSemaphore;
    vk::Semaphore renderFinishedSemaphore;
    vk::Fence inFlightFence;

    vk::CommandPool commandPool;
    vk::CommandBuffer mainCommandBuffer;

    AllocatedBuffer cameraBuffer;
    AllocatedBuffer objectBuffer;
    AllocatedBuffer lightBuffer;

    vk::DescriptorSet globalDescriptor;
    vk::DescriptorSet objectDescriptor;

    DeletionQueue frameDeletionQueue;
};

struct UploadContext {
    vk::Fence uploadFence;
    vk::CommandPool commandPool;
    vk::CommandBuffer commandBuffer;
};

struct MeshPushConstants {
    glm::vec4 data;
    glm::mat4 renderMatrix;
};

struct GPUSceneData {
    glm::vec4 fogColor; //w is exponent
    glm::vec4 fogDistances; //zw unused
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection; //w is intensity
    glm::vec4 sunlightColor; //w is shininess
};

struct GPUObjectData {
    glm::mat4 modelMatrix;
};

struct PointLightData {
    glm::vec4 worldPosition;
    glm::vec4 lightColor; //w is shininess
};

struct Texture {
    AllocatedImage image;
    vk::ImageView imageView;
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

    Material * createMaterial(vk::Pipeline pipeline, vk::PipelineLayout layout, const std::string& name);

    //TODO: make these return a Result struct instead of nullptr on failure
    //https://github.com/bitwizeshift/result
    Material * getMaterial(const std::string& name);
    Mesh * getMesh(const std::string& name);

    void drawObjects(vk::CommandBuffer cmd, RenderObject * first, int count);
private:
    //
    // Private members
    //
    bool m_isInitialized = false;
    uint64_t m_frameNumber = 0;
    float m_simulationTime = 0.0f; //Simulation time in seconds
    struct SDL_Window* m_sdlWindow = nullptr;
    int m_selectedShader = 0;
    bool m_framebufferResized = false;
    DeletionQueue m_mainDeletionQueue;
    DeletionQueue m_pipelineDeletionQueue;
    DeletionQueue m_sceneDeletionQueue;

    vma::Allocator m_allocator;
    UploadContext m_uploadContext; //context for uploading data (meshes, textures) to GPU memory

    // Vulkan members and handles
    vk::Extent2D m_windowExtent{1024, 768};
    vk::Instance m_instance;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    vk::PhysicalDevice m_activeGPU;
    vk::Device m_vkDevice;
    vk::SurfaceKHR m_vkSurface;
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;
    vk::RenderPass m_renderPass;
    vk::PhysicalDeviceProperties m_gpuProperties;
    vk::SampleCountFlagBits m_msaaSamples;

    FrameData m_frames[FRAMES_IN_FLIGHT];

    //Swap chain
    vk::SwapchainKHR m_swapChain;
    std::vector<vk::Image> m_swapChainImages;
    vk::Format m_swapChainImageFormat;
    vk::Extent2D m_swapChainExtent;
    std::vector<vk::ImageView> m_swapChainImageViews;
    std::vector<vk::Framebuffer> m_swapChainFramebuffers;

    //Depth buffer
    AllocatedImage m_depthImage;
    vk::ImageView m_depthImageView;
    vk::Format m_depthFormat;

    //MSAA images
    AllocatedImage m_colorImage;
    vk::ImageView m_colorImageView;
    vk::Format m_colorFormat;


    vk::PipelineLayout m_meshPipelineLayout;

    vk::Pipeline m_meshPipeline;

    //Vector of objects in the scene
    std::vector<RenderObject> m_renderables;
    RenderObject m_mine;
    //Materials, indexed by material name
    //TODO: these should not be stringly typed
    std::unordered_map<std::string, Material> m_materials;
    //Meshes, indexed by mesh name
    std::unordered_map<std::string, Mesh> m_meshes;
    //Textures, indexed by texture name
    std::vector<Texture> m_textures;
    //Terrain textures
    std::vector<Texture> m_terrainTextures;

    GPUSceneData m_sceneParameters;
    AllocatedBuffer m_sceneParameterBuffer;

    //Descriptor sets
    vk::DescriptorPool m_descriptorPool;
    vk::DescriptorSetLayout m_globalDescriptorSetLayout;
    vk::DescriptorSetLayout m_objectDescriptorSetLayout;
    vk::DescriptorSetLayout m_textureDescriptorSetLayout;
    vk::DescriptorSet m_textureDescriptorSet;
    vk::DescriptorSetLayout m_terrainTextureDescriptorSetLayout;
    vk::DescriptorSet m_terrainTextureDescriptorSet;

    //vk::Sampler m_nearestSampler;
    vk::Sampler m_linearSampler;

    camera m_camera;

    //
    //Terrain rendering stuff. This is here because this code is horrible.
    //splitting it into a separate class would be a pain because mesh allocation and uploading
    //isn't polymorphic and is built into the engine and I don't want to deal with that nonsense
    //
    const int m_terrainRenderDistance = 3;
    const int m_terrainChunkSize = 32;
    const unsigned int m_terrainSeed = 7u; //chosen by a fair dice roll. guaranteed to be random.
    const siv::PerlinNoise m_noiseSource{m_terrainSeed};

    // https://stackoverflow.com/questions/28367913/how-to-stdhash-an-unordered-stdpair
    struct pair_hash {
        template<typename T>
        void hash_combine(std::size_t &seed, T const &key) const {
            std::hash<T> hasher;
            seed ^= hasher(key) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        template <typename T1, typename T2>
        std::size_t operator()(std::pair<T1, T2> const &p) const {
            std::size_t seed(0);
            hash_combine(seed, p.first);
            hash_combine(seed, p.second);
            return seed;
        }
    };

    //why are these separate? because everything sucks, that's why
    std::unordered_map<std::pair<int, int>, Mesh, pair_hash> m_terrainMeshes;
    std::unordered_map<std::pair<int, int>, RenderObject, pair_hash> m_terrainRenderables;

    void generateTerrainChunk(int x, int z);
    void deleteTerrainChunk(int x, int z, DeletionQueue& deletionQueue);
    void deleteAllTerrainChunks();
    void updateTerrainChunks(DeletionQueue& deletionQueue);


    //
    // Private methods
    //

    void initVulkan();

    void initScene();

    bool checkValidationLayerSupport();

    bool checkDeviceExtensionSupport(const vk::PhysicalDevice & device);

    int scoreDevice(const vk::PhysicalDevice & device);

    QueueFamilyIndices findQueueFamilies(const vk::PhysicalDevice & device);

    //Swap chain functions
    SwapChainSupportDetails querySwapChainSupport(const vk::PhysicalDevice & device);
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availableModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR & capabilities);

    FrameData & getCurrentFrame();

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

    void createDescriptors();

    void createPipelines();

    void recreatePipelines();

    void recreateSwapChain();

    void cleanupSwapChain();


    //This will throw if the shader modules fail to load.
    vk::ShaderModule loadShaderModule(const char * filePath);

    void loadMeshes();
    void uploadMesh(Mesh &mesh, bool addToDeletionQueue = true);

    AllocatedBuffer createBuffer(size_t size, vk::BufferUsageFlags usageFlags, vma::MemoryUsage memoryUsage);
    void destroyBuffer(AllocatedBuffer buffer);

    size_t padUniformBufferSize(size_t originalSize);

    void submitImmediateCommand(std::function<void(vk::CommandBuffer cmd)> && function);

    Texture loadTexture(std::string file);
    void loadTextures();
    AllocatedImage loadImageFromFile(const char * filename);
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
    vk::PipelineDepthStencilStateCreateInfo m_depthStencil;

    vk::Pipeline buildPipeline(vk::Device device, vk::RenderPass pass);

};

#endif //VKENG_VK_ENGINE_H
