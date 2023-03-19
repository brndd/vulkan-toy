#ifndef VKENG_VK_DESCRIPTORS_H
#define VKENG_VK_DESCRIPTORS_H

#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_hash.hpp>


class DescriptorSetAllocator {
public:
    struct PoolSizes {
        std::vector<std::pair<vk::DescriptorType, float>> sizes = {
                { vk::DescriptorType::eSampler, 0.5f },
                { vk::DescriptorType::eCombinedImageSampler, 4.0f },
                { vk::DescriptorType::eSampledImage, 4.0f },
                { vk::DescriptorType::eStorageImage, 1.0f },
                { vk::DescriptorType::eUniformTexelBuffer, 1.0f },
                { vk::DescriptorType::eStorageTexelBuffer, 1.0f },
                { vk::DescriptorType::eUniformBuffer, 2.0f },
                { vk::DescriptorType::eStorageBuffer, 2.0f },
                { vk::DescriptorType::eUniformBufferDynamic, 1.0f },
                { vk::DescriptorType::eStorageBufferDynamic, 1.0f },
                { vk::DescriptorType::eInputAttachment, 0.5f }
        };
    };

    static const uint32_t defaultPoolSize = 1000;

    void resetPools();

    vk::DescriptorSet allocate(vk::DescriptorSetLayout layout);

    void init(vk::Device newDevice);

    void cleanup();

    vk::Device m_device;
private:
    vk::DescriptorPool m_currentPool = nullptr;
    PoolSizes m_descriptorSizes;
    std::vector<vk::DescriptorPool> m_usedPools;
    std::vector<vk::DescriptorPool> m_freePools;

    vk::DescriptorPool getPool();
};

class DescriptorSetLayoutCache {
public:
    void init(vk::Device device);
    void cleanup();

    vk::DescriptorSetLayout createDescriptorSetLayout(const vk::DescriptorSetLayoutCreateInfo &info);
private:
    std::unordered_map<vk::DescriptorSetLayoutCreateInfo, vk::DescriptorSetLayout> m_layoutCache;
    vk::Device m_device;
};

#endif //VKENG_VK_DESCRIPTORS_H

class DescriptorSetBuilder {
public:
    static DescriptorSetBuilder begin(DescriptorSetLayoutCache * layoutCache, DescriptorSetAllocator * allocator);

    DescriptorSetBuilder& bindBuffer(uint32_t binding, const vk::DescriptorBufferInfo & bufferInfo, const vk::DescriptorType & type, const vk::ShaderStageFlags & stageFlags);
    DescriptorSetBuilder& bindImage(uint32_t binding, const vk::DescriptorImageInfo & imageInfo, const vk::DescriptorType & type, const vk::ShaderStageFlags & stageFlags);

    std::pair<vk::DescriptorSet, vk::DescriptorSetLayout> build();
private:
    std::vector<vk::WriteDescriptorSet> m_writes;
    std::vector<vk::DescriptorSetLayoutBinding> m_bindings;

    DescriptorSetLayoutCache * m_cache;
    DescriptorSetAllocator * m_allocator;
};
