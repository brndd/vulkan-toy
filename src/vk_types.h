#ifndef VKENG_VK_TYPES_H
#define VKENG_VK_TYPES_H

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>
#include "vk_mesh.h"

#define S_TO_NS(x) (static_cast<uint64_t>(x * 1000000000))

struct AllocatedBuffer {
    vk::Buffer buffer;
    vma::Allocation allocation;
};

struct AllocatedImage {
    vk::Image image;
    vma::Allocation allocation;
};

struct Material {
    std::optional<vk::DescriptorSet> textureSet;
    vk::Pipeline pipeline;
    vk::PipelineLayout pipelineLayout;
};

struct RenderObject {
    Mesh * mesh;
    Material * material;
    glm::mat4 transformMatrix;
};

struct Texture {
    AllocatedImage image;
    vk::ImageView imageView;
};

#endif //VKENG_VK_TYPES_H
