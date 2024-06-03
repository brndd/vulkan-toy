#include "vk_mesh.h"
#include <tiny_obj_loader.h>
#include <iostream>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <PerlinNoise.hpp>

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

bool Mesh::loadFromHeightmap(const char *filename) {
    int mapX, mapY, mapN;
    int bytesPerPixel = 1;
    unsigned char * pixels = stbi_load(filename, &mapX, &mapY, &mapN, bytesPerPixel);
    if (!pixels) {
        return false;
    }

    //Lambda to sample heightmap at a given coordinate
    auto h = [&](int x, int y) -> unsigned char {
        x = std::clamp(x, 0, mapX - 1);
        y = std::clamp(y, 0, mapY - 1);
        return *(pixels + (x + y * mapY) * bytesPerPixel);
    };

    //the first pixel is top left in stb_image
    for (int i = 0; i < mapX; i++) {
        for (int j = 0; j < mapY; j++) {
            Vertex new_vertex;
            new_vertex.position.x = static_cast<float>(-mapX / 2.0f + i);
            new_vertex.position.z = static_cast<float>(-mapY / 2.0f + j);
            new_vertex.position.y = static_cast<float>(h(i, j)) / 255.0f * 100.0f;

            //UV
            new_vertex.uv.x = static_cast<float>(i) / (mapX - 1);
            new_vertex.uv.y = static_cast<float>(j) / (mapY - 1);

            //Calculate normals
            float rh, lh, bh, th;
            rh = static_cast<float>(h(i + 1, j)) / 255.0f * 100.0f;
            lh = static_cast<float>(h(i - 1, j)) / 255.0f * 100.0f;
            bh = static_cast<float>(h(i, j + 1)) / 255.0f * 100.0f;
            th = static_cast<float>(h(i, j - 1)) / 255.0f * 100.0f;
            glm::vec3 hor = {2.0f, rh - lh, 0.0f};
            glm::vec3 ver = {0.0f, bh - th, 2.0f};
            new_vertex.normal = glm::normalize(glm::cross(ver, hor));

            this->vertices.push_back(new_vertex);
        }
    }

    //Populate indices
    for (int i = 0; i < mapX - 1; i++) {
        for (int j = 0; j < mapY - 1; j++) {
            int start = i + j * mapX;
            indices.push_back(start);
            indices.push_back(start + 1);
            indices.push_back(start + mapX);
            indices.push_back(start + 1);
            indices.push_back(start + 1 + mapX);
            indices.push_back(start + mapX);
        }
    }

    stbi_image_free(pixels);
    return true;
}

bool Mesh::sampleFromNoise(int x, int z, int size, const siv::PerlinNoise& noiseSource) {
    auto worldPosX = static_cast<float>(x * (size - 1));
    auto worldPosZ = static_cast<float>(z * (size - 1));

    //Lambda to sample noise function at a given coordinate (in mesh space)
    auto h = [&](float x, float z) -> double {
        auto world_x = (worldPosX + x) * 0.01;
        auto world_z = (worldPosZ + z) * 0.01;
        return static_cast<float>(noiseSource.octave2D_01(world_x, world_z, 4));
    };

    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            auto pos_x = static_cast<float>(-size / 2.0f + i);
            auto pos_z = static_cast<float>(-size / 2.0f + j);
            float noise = h(pos_x, pos_z);
            Vertex new_vertex;
            new_vertex.position.x = pos_x;
            new_vertex.position.z = pos_z;
            new_vertex.position.y = noise * 100.0f;

            //UV
            new_vertex.uv.x = static_cast<float>(i) / (size - 1);
            new_vertex.uv.y = static_cast<float>(j) / (size - 1);

            //Normals
            float rh, lh, bh, th;
            rh = h(pos_x + 1, pos_z);
            lh = h(pos_x - 1, pos_z);
            bh = h(pos_x, pos_z + 1);
            th = h(pos_x, pos_z - 1);
            glm::vec3 hor = {2.0f, rh - lh, 0.0f};
            glm::vec3 ver = {0.0f, bh - th, 2.0f};
            new_vertex.normal = glm::normalize(glm::cross(ver, hor));

            this->vertices.push_back(new_vertex);
        }
    }

    //Indices
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - 1; j++) {
            int start = i + j * size;
            indices.push_back(start);
            indices.push_back(start + 1);
            indices.push_back(start + size);
            indices.push_back(start + 1);
            indices.push_back(start + 1 + size);
            indices.push_back(start + size);
        }
    }

    return true;
}


