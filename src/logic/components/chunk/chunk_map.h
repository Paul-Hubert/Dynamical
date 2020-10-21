#ifndef CHUNK_MAP_H
#define CHUNK_MAP_H

#include "util/quad_tree.h"

#include "entt/entt.hpp"

#include "glm/glm.hpp"


class Entity {
public:
    Entity() : value(entt::null) {}
    Entity(entt::entity entity) : value(entity) {}
    Entity& operator=(entt::entity entity) {
        value = entity;
        return *this;
    }
    entt::entity& get() {
        return value;
    }
    operator bool () {
        return value != entt::null;
    }
private:
    entt::entity value;
};

class ChunkMap {
public:
    
    entt::entity get(int x, int y, int z, int lod = 0) {
        return tree.get(x, y, z, lod).get();
    }
    
    void set(entt::entity entity, int x, int y, int z, int lod = 0) {
        tree.set(x, y, z, lod, Entity(entity));
    }
    void remove(int x, int y, int z) {
        tree.remove(x, y, z);
    }
    void print() {
        tree.print();
    }
    
    template <class Archive>
    void serialize( Archive & ar ) {
        ar(tree);
    }
    
private:
    QuadTree<Entity> tree;
    
};

class GlobalChunkMap : public ChunkMap {};

#endif
