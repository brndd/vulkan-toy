#ifndef VKENG_TERRAINCHUNKMANAGER_H
#define VKENG_TERRAINCHUNKMANAGER_H

#include <unordered_map>
#include "vk_mesh.h"

/*
 * This class handles generating, retrieving and deleting "chunks" (meshes) of terrain.
 * It generates terrain around a given point, deleting terrain that's outside the render distance.
 */

class TerrainChunkManager {
public:
    TerrainChunkManager(int renderDistance, int chunkSize, int seed) : m_renderDistance(renderDistance), m_chunkSize(chunkSize), m_seed(seed) {}
    ~TerrainChunkManager();

    void updatePosition(glm::vec3 position);
    std::vector<Mesh*> getChunks();

private:
    int m_renderDistance;
    int m_chunkSize;
    int m_seed;

    // https://stackoverflow.com/questions/28367913/how-to-stdhash-an-unordered-stdpair
    struct pair_hash {
        template<typename T>
        void hash_combine(std::size_t &seed, T const &key) {
            std::hash<T> hasher;
            seed ^= hasher(key) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        template <class T1, class T2>
        std::size_t operator()(std::pair<T1, T2> const &p) const {
            std::size_t seed(0);
            hash_combine(seed, p.first);
            hash_combine(seed, p.second);
            return seed;
        }
    };

    std::unordered_map<std::pair<int, int>, Mesh, pair_hash> m_chunks;

    void generateChunk(int x, int z);
    void deleteChunk(int x, int z);
    void deleteChunksOutsideRenderDistance();
    void deleteAllChunks();
};


#endif //VKENG_TERRAINCHUNKMANAGER_H
