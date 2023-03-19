#include "vk_descriptors.h"
#include <algorithm>

void DescriptorSetAllocator::init(vk::Device newDevice) {
    m_device = newDevice;
}

void DescriptorSetAllocator::cleanup() {
    //Delete all held pools
    for (auto pool : m_freePools) {
        m_device.destroyDescriptorPool(pool);
    }
    for (auto pool : m_usedPools) {
        m_device.destroyDescriptorPool(pool);
    }
}

/**
 * Create a new descriptor pool. This will be added to the free pool list.
 * @param device vk::Device to create the descriptor pool on
 * @param poolSizes Multipliers for each descriptor type. The pool will contain count * multiplier descriptors of each type
 * @param count Number of descriptors to allocate
 * @param flags Flags to pass to the descriptor pool
 * @return
 */
vk::DescriptorPool createPool(vk::Device device, const DescriptorSetAllocator::PoolSizes& poolSizes, uint32_t count = DescriptorSetAllocator::defaultPoolSize, vk::DescriptorPoolCreateFlags flags = vk::DescriptorPoolCreateFlags{}) {
    std::vector<vk::DescriptorPoolSize> sizes;
    sizes.reserve(poolSizes.sizes.size());


    for (auto size : poolSizes.sizes) {
        sizes.push_back({ size.first, static_cast<uint32_t>(size.second * count) });
    }

    vk::DescriptorPoolCreateInfo info = {};
    info.flags = flags;
    info.maxSets = count;
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.setPoolSizes(sizes);

    vk::DescriptorPool pool = device.createDescriptorPool(info);
    return pool;
}

vk::DescriptorPool DescriptorSetAllocator::getPool() {
    //Use a free pool if there are any available
    if (!m_freePools.empty()) {
        auto pool = m_freePools.back();
        m_freePools.pop_back();
        return pool;
    }
    else {
        return createPool(m_device, m_descriptorSizes);
    }
}

vk::DescriptorSet DescriptorSetAllocator::allocate(vk::DescriptorSetLayout layout) {
    if (!m_currentPool) {
        m_currentPool = getPool();
        m_usedPools.push_back(m_currentPool);
    }

    vk::DescriptorSetAllocateInfo info = {};
    info.setSetLayouts(layout);
    info.descriptorPool = m_currentPool;
    info.descriptorSetCount = 1;

    //Try to allocate
    try {
        return m_device.allocateDescriptorSets(info)[0];
    }
    catch(vk::FragmentedPoolError const & e) {
        //Ignore these exceptions
    }
    catch(vk::OutOfPoolMemoryError const & e) {
        //Ignore these exceptions
    }

    //If we failed due to FragmentedPoolError or OutOfPoolMemoryError, we'll try to create a new pool
    m_currentPool = getPool();
    m_usedPools.push_back(m_currentPool);

    //If this still fails, we have a fatal error, so we're just letting the exception propagate.
    return m_device.allocateDescriptorSets(info)[0];
}

void DescriptorSetAllocator::resetPools() {
    for (auto pool : m_usedPools) {
        m_device.resetDescriptorPool(pool);
        m_freePools.push_back(pool);
    }

    m_usedPools.clear();
    m_currentPool = nullptr;
}

void DescriptorSetLayoutCache::init(vk::Device device) {
    m_device = device;
}

void DescriptorSetLayoutCache::cleanup() {
    for (const auto& pair : m_layoutCache) {
        m_device.destroyDescriptorSetLayout(pair.second);
    }
}

vk::DescriptorSetLayout
DescriptorSetLayoutCache::createDescriptorSetLayout(const vk::DescriptorSetLayoutCreateInfo &info) {
    //Check if binding is cached
    auto it = m_layoutCache.find(info);
    if (it != m_layoutCache.end()) {
        return it->second;
    }
    //If not, create a new one and cache it
    else {
        vk::DescriptorSetLayout layout = m_device.createDescriptorSetLayout(info);
        m_layoutCache[info] = layout;
        return layout;
    }
}

DescriptorSetBuilder
DescriptorSetBuilder::begin(DescriptorSetLayoutCache *layoutCache, DescriptorSetAllocator *allocator) {
    DescriptorSetBuilder builder;
    builder.m_cache = layoutCache;
    builder.m_allocator = allocator;
    return builder;
}

DescriptorSetBuilder &DescriptorSetBuilder::bindBuffer(uint32_t binding, const vk::DescriptorBufferInfo &bufferInfo,
                                                       const vk::DescriptorType &type,
                                                       const vk::ShaderStageFlags &stageFlags) {
    vk::DescriptorSetLayoutBinding newBinding = {};

    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;
    m_bindings.push_back(newBinding);

    vk::WriteDescriptorSet newWrite = {};
    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.setBufferInfo(bufferInfo);
    newWrite.dstBinding = binding;
    m_writes.push_back(newWrite);

    return *this;
}

DescriptorSetBuilder &DescriptorSetBuilder::bindImage(uint32_t binding, const vk::DescriptorImageInfo &imageInfo,
                                                      const vk::DescriptorType &type,
                                                      const vk::ShaderStageFlags &stageFlags) {
    vk::DescriptorSetLayoutBinding newBinding = {};

    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;
    m_bindings.push_back(newBinding);

    vk::WriteDescriptorSet newWrite = {};
    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.setImageInfo(imageInfo);
    newWrite.dstBinding = binding;
    m_writes.push_back(newWrite);

    return *this;
}

std::pair<vk::DescriptorSet, vk::DescriptorSetLayout> DescriptorSetBuilder::build() {
    //Build layout
    vk::DescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.setBindings(m_bindings);
    vk::DescriptorSetLayout layout = m_cache->createDescriptorSetLayout(layoutInfo);

    //Allocate descriptor set
    vk::DescriptorSet set = m_allocator->allocate(layout);

    //Write descriptor set
    for (auto & w : m_writes) {
        w.dstSet = set;
    }

    m_allocator->m_device.updateDescriptorSets(m_writes, {});

    return {set, layout};
}
