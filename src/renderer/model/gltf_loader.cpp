#include "gltf_loader.h"

#include "util/log.h"

#include "renderer/context/context.h"

// tinygltf decodes images with stb_image, whose implementation also serves renderer/util/texture.cpp
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>
#include <functional>
#include <stdexcept>

using namespace dy;

namespace {

// Accessor converted to floats (normalized integers mapped to [0,1] / [-1,1])
std::vector<float> readFloats(const tinygltf::Model& m, int index, int& components) {

    const auto& acc = m.accessors.at(index);
    components = tinygltf::GetNumComponentsInType(acc.type);
    int size = tinygltf::GetComponentSizeInBytes(acc.componentType);
    if(components <= 0 || size <= 0) {
        throw std::runtime_error("unsupported accessor type");
    }

    std::vector<float> out(acc.count * components, 0.f);
    if(acc.bufferView < 0) {
        return out;
    }

    const auto& view = m.bufferViews.at(acc.bufferView);
    const auto& buffer = m.buffers.at(view.buffer).data;
    int stride = acc.ByteStride(view);
    size_t start = view.byteOffset + acc.byteOffset;
    if(stride <= 0 || start + (acc.count ? (acc.count - 1) * stride + components * size : 0) > buffer.size()) {
        throw std::runtime_error("accessor out of buffer bounds");
    }

    for(size_t i = 0; i < acc.count; i++) {
        const uint8_t* elem = buffer.data() + start + i * stride;
        for(int c = 0; c < components; c++) {
            float v = 0.f;
            switch(acc.componentType) {
                case TINYGLTF_COMPONENT_TYPE_BYTE:           { int8_t x;   std::memcpy(&x, elem + c, 1);     v = acc.normalized ? std::max(x / 127.f, -1.f) : x; break; }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  { uint8_t x;  std::memcpy(&x, elem + c, 1);     v = acc.normalized ? x / 255.f : x; break; }
                case TINYGLTF_COMPONENT_TYPE_SHORT:          { int16_t x;  std::memcpy(&x, elem + c * 2, 2); v = acc.normalized ? std::max(x / 32767.f, -1.f) : x; break; }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t x; std::memcpy(&x, elem + c * 2, 2); v = acc.normalized ? x / 65535.f : x; break; }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   { uint32_t x; std::memcpy(&x, elem + c * 4, 4); v = (float) x; break; }
                case TINYGLTF_COMPONENT_TYPE_FLOAT:          { std::memcpy(&v, elem + c * 4, 4); break; }
            }
            out[i * components + c] = v;
        }
    }

    return out;

}

std::vector<uint32_t> readIndices(const tinygltf::Model& m, int index) {

    const auto& acc = m.accessors.at(index);
    const auto& view = m.bufferViews.at(acc.bufferView);
    const auto& buffer = m.buffers.at(view.buffer).data;
    int size = tinygltf::GetComponentSizeInBytes(acc.componentType);
    int stride = acc.ByteStride(view);
    size_t start = view.byteOffset + acc.byteOffset;
    if(size <= 0 || stride <= 0 || start + (acc.count ? (acc.count - 1) * stride + size : 0) > buffer.size()) {
        throw std::runtime_error("index accessor out of buffer bounds");
    }

    std::vector<uint32_t> out(acc.count);
    for(size_t i = 0; i < acc.count; i++) {
        uint32_t v = 0;
        std::memcpy(&v, buffer.data() + start + i * stride, size);
        out[i] = v;
    }
    return out;

}

glm::mat4 nodeMatrix(const tinygltf::Node& node) {
    if(node.matrix.size() == 16) {
        glm::mat4 matrix;
        for(int i = 0; i < 16; i++) glm::value_ptr(matrix)[i] = (float) node.matrix[i];
        return matrix;
    }
    glm::mat4 matrix(1.f);
    if(node.translation.size() == 3) {
        matrix = glm::translate(matrix, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    }
    if(node.rotation.size() == 4) { // x y z w
        matrix = matrix * glm::mat4_cast(glm::quat((float) node.rotation[3], (float) node.rotation[0], (float) node.rotation[1], (float) node.rotation[2]));
    }
    if(node.scale.size() == 3) {
        matrix = glm::scale(matrix, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    }
    return matrix;
}

}

ModelC dy::loadGLTF(entt::registry& reg, const std::string& path) {

    tinygltf::TinyGLTF loader;
    tinygltf::Model m;
    std::string err, warn;

    bool binary = path.size() > 4 && path.substr(path.size() - 4) == ".glb";
    bool ok = binary ? loader.LoadBinaryFromFile(&m, &err, &warn, path) : loader.LoadASCIIFromFile(&m, &err, &warn, path);

    if(!warn.empty()) {
        dy::log(dy::warning) << "glTF : " << warn << "\n";
    }
    if(!ok) {
        throw std::runtime_error(err.empty() ? "tinygltf could not load the file" : err);
    }

    Context& ctx = *reg.ctx().get<Context*>();

    ModelC model;

    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;


    // Materials, images are uploaded once even if shared

    std::vector<std::shared_ptr<VmaImage>> images(m.images.size());

    auto getImage = [&](int texture_index) -> std::shared_ptr<VmaImage> {
        if(texture_index < 0 || texture_index >= (int) m.textures.size()) return nullptr;
        int source = m.textures[texture_index].source;
        if(source < 0 || source >= (int) m.images.size()) return nullptr;
        if(images[source]) return images[source];

        auto& image = m.images[source];
        if(image.image.empty() || image.component != 4 || image.bits != 8) {
            dy::log(dy::warning) << "glTF : unsupported image " << source << " of " << path << " (" << image.component << " channels, " << image.bits << " bits)\n";
            return nullptr;
        }

        // Base color is authored in sRGB
        vk::ImageCreateInfo info({}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Srgb, vk::Extent3D(image.width, image.height, 1), 1, 1, vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive, 1, &ctx.device.g_i, vk::ImageLayout::eUndefined);
        images[source] = ctx.transfer.createImage(image.image.data(), image.image.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal);

        return images[source];
    };

    for(auto& mat : m.materials) {
        ModelC::Material material;
        auto& factor = mat.pbrMetallicRoughness.baseColorFactor;
        if(factor.size() == 4) {
            material.base_color = glm::vec4(factor[0], factor[1], factor[2], factor[3]);
        }
        material.image = getImage(mat.pbrMetallicRoughness.baseColorTexture.index);
        model.materials.push_back(material);
    }

    // Primitives without a material
    uint32_t default_material = (uint32_t) model.materials.size();
    model.materials.push_back(ModelC::Material());


    // Meshes, instanced through the node hierarchy of the default scene

    auto addMesh = [&](int mesh_index, const glm::mat4& transform) {

        glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));

        for(auto& primitive : m.meshes.at(mesh_index).primitives) {

            if(primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                dy::log(dy::warning) << "glTF : skipping non triangle primitive in " << path << "\n";
                continue;
            }

            auto position_it = primitive.attributes.find("POSITION");
            if(position_it == primitive.attributes.end()) continue;

            int components;
            auto positions = readFloats(m, position_it->second, components);
            size_t count = positions.size() / components;

            std::vector<float> normals, uvs;
            int normal_components = 3, uv_components = 2;
            if(auto it = primitive.attributes.find("NORMAL"); it != primitive.attributes.end()) normals = readFloats(m, it->second, normal_components);
            if(auto it = primitive.attributes.find("TEXCOORD_0"); it != primitive.attributes.end()) uvs = readFloats(m, it->second, uv_components);

            uint32_t base_vertex = (uint32_t) vertices.size();
            for(size_t i = 0; i < count; i++) {
                ModelVertex v;
                v.position = glm::vec3(transform * glm::vec4(positions[i * components], positions[i * components + 1], positions[i * components + 2], 1.f));
                v.normal = normals.empty() ? glm::vec3(0.f, 1.f, 0.f)
                    : glm::normalize(normal_matrix * glm::vec3(normals[i * normal_components], normals[i * normal_components + 1], normals[i * normal_components + 2]));
                v.uv = uvs.empty() ? glm::vec2(0.f) : glm::vec2(uvs[i * uv_components], uvs[i * uv_components + 1]);
                vertices.push_back(v);
            }

            std::vector<uint32_t> prim_indices;
            if(primitive.indices >= 0) {
                prim_indices = readIndices(m, primitive.indices);
            } else {
                prim_indices.resize(count);
                for(uint32_t i = 0; i < count; i++) prim_indices[i] = i;
            }

            // Flat normals when the file has none
            if(normals.empty()) {
                for(size_t i = 0; i + 2 < prim_indices.size(); i += 3) {
                    auto& a = vertices[base_vertex + prim_indices[i]];
                    auto& b = vertices[base_vertex + prim_indices[i + 1]];
                    auto& c = vertices[base_vertex + prim_indices[i + 2]];
                    glm::vec3 n = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
                    a.normal = b.normal = c.normal = n;
                }
            }

            // A negative scale flips the winding
            bool flip = glm::determinant(glm::mat3(transform)) < 0.f;

            ModelC::Part part;
            part.first_index = (uint32_t) indices.size();
            part.index_count = (uint32_t) (prim_indices.size() / 3 * 3);
            part.material = primitive.material >= 0 && primitive.material < (int) default_material ? (uint32_t) primitive.material : default_material;
            for(size_t i = 0; i + 2 < prim_indices.size(); i += 3) {
                indices.push_back(base_vertex + prim_indices[i]);
                indices.push_back(base_vertex + prim_indices[i + (flip ? 2 : 1)]);
                indices.push_back(base_vertex + prim_indices[i + (flip ? 1 : 2)]);
            }
            model.parts.push_back(part);

        }

    };

    std::function<void(int, const glm::mat4&)> visit = [&](int node_index, const glm::mat4& parent) {
        const auto& node = m.nodes.at(node_index);
        glm::mat4 transform = parent * nodeMatrix(node);
        if(node.mesh >= 0) {
            addMesh(node.mesh, transform);
        }
        for(int child : node.children) {
            visit(child, transform);
        }
    };

    if(!m.scenes.empty()) {
        int scene = m.defaultScene >= 0 ? m.defaultScene : 0;
        for(int node : m.scenes.at(scene).nodes) {
            visit(node, glm::mat4(1.f));
        }
    } else {
        for(int mesh = 0; mesh < (int) m.meshes.size(); mesh++) {
            addMesh(mesh, glm::mat4(1.f));
        }
    }

    if(vertices.empty() || indices.empty()) {
        throw std::runtime_error("no triangles");
    }

    model.min = model.max = vertices[0].position;
    for(auto& v : vertices) {
        model.min = glm::min(model.min, v.position);
        model.max = glm::max(model.max, v.position);
    }

    model.vertices = ctx.transfer.createBuffer(vertices.data(), vk::BufferCreateInfo({}, vertices.size() * sizeof(ModelVertex),
        vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));
    model.indices = ctx.transfer.createBuffer(indices.data(), vk::BufferCreateInfo({}, indices.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eIndexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i));

    dy::log(dy::info) << "Loaded " << path << " : " << vertices.size() << " vertices, " << indices.size() / 3 << " triangles\n";

    return model;

}
