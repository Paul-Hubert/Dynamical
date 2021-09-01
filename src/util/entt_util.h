#ifndef ENTT_UTIL_H
#define ENTT_UTIL_H

#include "entt/entt.hpp"

namespace dy {
    
template<typename entt::hashed_string::hash_type Value, typename T>
class Alias {
public:
    Alias(T t) : value(t) {};
    operator T() {
        return value;
    }
private:
    T value;
};

template<typename entt::hashed_string::hash_type Value>
class Entity {
public:
    Entity(entt::entity t) : value(t) {};
    operator entt::entity() {
        return value;
    }
private:
    entt::entity value;
};

}

#endif
