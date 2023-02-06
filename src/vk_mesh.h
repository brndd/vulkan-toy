#ifndef VKENG_VK_MESH_H
#define VKENG_VK_MESH_H

#include "vk_types.h"
#include <vector>
#include <glm/vec3.hpp>

struct VertexInputDescription {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
    vk::PipelineVertexInputStateCreateFlags flags;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;

    static VertexInputDescription getVertexDescription();
};

struct Mesh {
    std::vector<Vertex> vertices;
    AllocatedBuffer vertexBuffer;
};


#endif //VKENG_VK_MESH_H
