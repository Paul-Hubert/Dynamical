#include "gltf_loader.h"

#include "util/util.h"
#include "renderer/context.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

using namespace tinygltf;

GLTFLoader::GLTFLoader(entt::registry& reg) : reg(reg) {

}

entt::entity GLTFLoader::load(std::string path) {

	Model m;
	std::string err;
	std::string warn;

	bool binary = false;
	size_t extpos = path.rfind('.', path.length());
	if(extpos != std::string::npos) {
		binary = (path.substr(extpos + 1, path.length() - extpos) == "glb");
	}

	bool ret = binary ? loader.LoadBinaryFromFile(&m, &err, &warn, path) : loader.LoadASCIIFromFile(&m, &err, &warn, path);

	if(!warn.empty()) {
		Util::log(Util::warning) << warn << "\n";
	}

	if(!err.empty()) {
		Util::log(Util::error) << err << "\n";
	}

	if(!ret) {
		throw std::runtime_error("Could not load gltf file");
	}

	Context& ctx = *reg.ctx<Context*>();

	entt::entity entity = reg.create();
	ModelC& model = reg.emplace<ModelC>(entity);

	std::vector<entt::entity> buffers;
	for(auto& b : m.buffers) {
		entt::entity buffer = reg.create();
		vk::BufferCreateInfo info({}, b.data.size(), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i);
		auto& vertex = reg.emplace<BufferUploadC>(buffer, ctx, info);
		memcpy(vertex.data, b.data.data(), b.data.size());
		buffers.push_back(buffer);
	}

	for(auto& mesh : m.meshes) {
		for(auto& primitive : mesh.primitives) {
			if(primitive.mode != TINYGLTF_MODE_TRIANGLES) {
				Util::log(Util::warning) << "GLTFLoader : Rendering mode not supported\n";
				continue;
			}

			ModelC::Part part;
			
			makePart(m, primitive.indices, part.index, buffers, 0);

			{
				auto it = primitive.attributes.find("POSITION");
				if(it == primitive.attributes.end())
					Util::log(Util::warning) << "GLTFLoader : Position needed\n";
				makePart(m, it->second, part.position, buffers, 1);
			}

			{
				auto it = primitive.attributes.find("NORMAL");
				if(it == primitive.attributes.end())
					Util::log(Util::warning) << "GLTFLoader : Position needed\n";
				makePart(m, it->second, part.normal, buffers, 1);
			}

			model.parts.push_back(part);

		}
	}

	return entity;

}

void GLTFLoader::makePart(Model& m, int index, ModelC::Part::View& v, std::vector<entt::entity>& buffers, int type) {
	auto& acc = m.accessors[index];
	auto& view = m.bufferViews[acc.bufferView];

	if(type == 0) {
		if(acc.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
			Util::log(Util::warning) << "GLTFLoader : Component type not supported\n";
		if(acc.type != TINYGLTF_TYPE_SCALAR)
			Util::log(Util::warning) << "GLTFLoader : Type not supported\n";
	} else if(type == 1) {
		if(acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
			Util::log(Util::warning) << "GLTFLoader : Component type not supported\n";
		if(acc.type != TINYGLTF_TYPE_VEC3)
			Util::log(Util::warning) << "GLTFLoader : Type not supported\n";
	} else if(type == 2) {
		if(acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
			Util::log(Util::warning) << "GLTFLoader : Component type not supported\n";
		if(acc.type != TINYGLTF_TYPE_VEC2)
			Util::log(Util::warning) << "GLTFLoader : Type not supported\n";
	}

	v.buffer = buffers[view.buffer];
	v.size = acc.count * GetNumComponentsInType(acc.type) * GetComponentSizeInBytes(acc.componentType);
	v.offset = acc.byteOffset + view.byteOffset;
}

GLTFLoader::~GLTFLoader() {

}