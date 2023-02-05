#include "vk_mesh.h"

VertexInputDescription Vertex::get_vertex_description() {
    VertexInputDescription description;

    //Quoth the guide: "We will have just 1 vertex buffer binding, with a per-vertex rate"
    vk::VertexInputBindingDescription mainBinding = {};
    mainBinding.binding = 0;
    mainBinding.stride = sizeof(Vertex);
    mainBinding.inputRate = vk::VertexInputRate::eVertex;
    description.bindings.push_back(mainBinding);

    //Position stored at location 0
    vk::VertexInputAttributeDescription positionAttribute = {};
    positionAttribute.binding = 0;
    positionAttribute.location = 0;
    positionAttribute.format = vk::Format::eR32G32B32A32Sfloat;
    positionAttribute.offset = offsetof(Vertex, position);

    //Normal stored at location 1
    vk::VertexInputAttributeDescription normalAttribute = {};
    normalAttribute.binding = 0;
    normalAttribute.location = 1;
    normalAttribute.format = vk::Format::eR32G32B32Sfloat;
    normalAttribute.offset = offsetof(Vertex, normal);

    //Color stored at location 2
    vk::VertexInputAttributeDescription colorAttribute = {};
    colorAttribute.binding = 0;
    colorAttribute.location = 2;
    colorAttribute.format = vk::Format::eR32G32B32Sfloat;
    colorAttribute.offset = offsetof(Vertex, color);

    description.attributes.push_back(positionAttribute);
    description.attributes.push_back(normalAttribute);
    description.attributes.push_back(colorAttribute);
    return description;
}
