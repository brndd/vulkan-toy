#ifndef VKENG_VK_MESH_H
#define VKENG_VK_MESH_H

#include "vk_types.h"
#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <PerlinNoise.hpp>

struct VertexInputDescription {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
    vk::PipelineVertexInputStateCreateFlags flags;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;

    static VertexInputDescription getVertexDescription();
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;

    bool loadFromObj(const char* filename);
    bool loadFromHeightmap(const char* filename);
    bool flatPlane(int x, int z, int size);
    bool sampleFromNoise(int x, int z, int size, const siv::PerlinNoise& noiseSource);
};


#endif //VKENG_VK_MESH_H
