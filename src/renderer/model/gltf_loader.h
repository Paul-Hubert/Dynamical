#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "entt/entt.hpp"

#include <string>

#include "model.h"

namespace dy {

// glTF 2.0 (.gltf / .glb) through tinygltf, flattened into one vertex and index buffer.
// Triangle primitives only; POSITION, NORMAL, TEXCOORD_0 and indices; base color factor and texture.
// Node transforms of the default scene are baked into the vertices. No skins, morph targets or sparse accessors.
// Throws std::runtime_error on malformed files.
ModelC loadGLTF(entt::registry& reg, const std::string& path);

}

#endif
