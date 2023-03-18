#include "vk_descriptors.h"

void DescriptorAllocator::init(vk::Device newDevice) {
    m_device = newDevice;
}

void DescriptorAllocator::cleanup() {
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
vk::DescriptorPool createPool(vk::Device device, const DescriptorAllocator::PoolSizes& poolSizes, uint32_t count = DescriptorAllocator::defaultPoolSize, vk::DescriptorPoolCreateFlags flags = vk::DescriptorPoolCreateFlags{}) {
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

vk::DescriptorPool DescriptorAllocator::getPool() {
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

vk::DescriptorSet DescriptorAllocator::allocate(vk::DescriptorSetLayout layout) {
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

void DescriptorAllocator::resetPools() {
    for (auto pool : m_usedPools) {
        m_device.resetDescriptorPool(pool);
        m_freePools.push_back(pool);
    }

    m_usedPools.clear();
    m_currentPool = nullptr;
}
