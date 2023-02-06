#ifndef VKENG_VK_TYPES_H
#define VKENG_VK_TYPES_H

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

#define S_TO_NS(x) (static_cast<uint64_t>(x * 1000000000))

struct AllocatedBuffer {
    vk::Buffer buffer;
    vma::Allocation allocation;
};

#endif //VKENG_VK_TYPES_H
