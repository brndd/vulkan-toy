#include "vk_mesh.h"
#include <tiny_obj_loader.h>
#include <iostream>

VertexInputDescription Vertex::getVertexDescription() {
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

    //UV stored at location 3
    vk::VertexInputAttributeDescription uvAttribute = {};
    uvAttribute.binding = 0;
    uvAttribute.location = 3;
    uvAttribute.format = vk::Format::eR32G32Sfloat;
    uvAttribute.offset = offsetof(Vertex, uv);

    description.attributes.push_back(positionAttribute);
    description.attributes.push_back(normalAttribute);
    description.attributes.push_back(colorAttribute);
    description.attributes.push_back(uvAttribute);
    return description;
}

bool Mesh::loadFromObj(const char *filename) {
    tinyobj::ObjReaderConfig readerConfig;
    readerConfig.mtl_search_path = "data/assets/";

    tinyobj::ObjReader reader;

    if (!reader.ParseFromFile(filename, readerConfig)) {
        if (!reader.Error().empty()) {
            std::cerr << "[ERR] TinyObjReader: " << reader.Error();
        }
        return false;
    }
    if (!reader.Warning().empty()) {
        std::cout << "[WARN] TinyObjReader: " << reader.Warning();
    }

    auto & attrib = reader.GetAttrib();
    auto & shapes = reader.GetShapes();
    auto & materials = reader.GetMaterials();

    //This is straight ripped off from the tinyobjloader examples and let me tell you it's kinda horrible
    //Iterate over shapes
    for (size_t s = 0; s < shapes.size(); s++) {
        size_t index_offset = 0;

        //Iterate over faces of the shape
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
            size_t fv = shapes[s].mesh.num_face_vertices[f];
            if (fv != 3) {
                std::cerr << "[ERR] TinyObjReader: too many vertices (" << fv << ") on face in mesh " << filename << std::endl;
                return false;
            }

            //Iterate over vertices of the face
            for (size_t v = 0; v < fv; v++) {
                Vertex new_vertex;

                //access to vertex
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];

                new_vertex.position.x = vx;
                new_vertex.position.y = vy;
                new_vertex.position.z = vz;

                //Check if normal_index is zero or positive. Negative = no normal data.
                if (idx.normal_index >= 0) {
                    tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
                    tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
                    tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

                    new_vertex.normal.x = nx;
                    new_vertex.normal.y = ny;
                    new_vertex.normal.z = nz;

                    //Set the vertex color to normal coords for visualization purposes
                    new_vertex.color = new_vertex.normal;
                }

                //Check if texcoord_index is zero or positive. Negative = no uv data.
                if (idx.texcoord_index >= 0) {
                    tinyobj::real_t tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                    tinyobj::real_t ty = attrib.texcoords[2 * idx.texcoord_index + 1];

                    new_vertex.uv.x = tx;
                    new_vertex.uv.y = 1 - ty;
                }

                // Optional: vertex colors
                // tinyobj::real_t red   = attrib.colors[3 * idx.vertex_index + 0];
                // tinyobj::real_t green = attrib.colors[3 * idx.vertex_index + 1];
                // tinyobj::real_t blue  = attrib.colors[3 * idx.vertex_index + 2];

                this->vertices.push_back(new_vertex);
            }
            index_offset += fv;
            //Per-face material
            //shapes[s].mesh.material_ids[f];
        }
    }

    return true;
}
