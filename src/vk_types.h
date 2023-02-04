#ifndef VKENG_VK_TYPES_H
#define VKENG_VK_TYPES_H

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

struct AllocatedBuffer {
    vk::Buffer buffer;
    vma::Allocation allocation;
};

#endif //VKENG_VK_TYPES_H
