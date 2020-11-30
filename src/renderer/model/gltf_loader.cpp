#include "gltf_loader.h"

#include "util/util.h"
#include "renderer/context.h"
#include "renderer/model/materialc.h"


#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
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

	entt::entity model_entity = reg.create();
	ModelC& model = reg.emplace<ModelC>(model_entity);

	std::vector<std::shared_ptr<BufferC>> buffers;
	for(auto& b : m.buffers) {
		vk::BufferCreateInfo info({}, b.data.size(), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive, 1, &ctx.device.g_i);
		buffers.push_back(ctx.transfer.createBuffer(b.data.data(), info));
	}

	std::vector<std::shared_ptr<ImageC>> images;
	for(auto& i : m.images) {
		vk::Format format = vk::Format::eR8G8B8A8Unorm;
		vk::ImageCreateInfo info({}, vk::ImageType::e2D, format, vk::Extent3D(i.width, i.height, 1), 1, 1, vk::SampleCountFlagBits::e1,
			vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
			vk::SharingMode::eExclusive, 1, & ctx.device.g_i, vk::ImageLayout::eUndefined);
		images.push_back(ctx.transfer.createImage(i.image.data(), i.image.size(), info, vk::ImageLayout::eShaderReadOnlyOptimal));
	}

	std::vector<std::shared_ptr<MaterialC>> materials;
	for(auto& mat : m.materials) {
		materials.push_back(std::make_shared<MaterialC>(images[mat.pbrMetallicRoughness.baseColorTexture.index]));
	}

	model.mass = 10;

	model.tivma = std::make_unique<btTriangleIndexVertexArray>();

	for(auto& mesh : m.meshes) {
		for(auto& primitive : mesh.primitives) {
			if(primitive.mode != TINYGLTF_MODE_TRIANGLES) {
				Util::log(Util::warning) << "GLTFLoader : Rendering mode not supported\n";
				continue;
			}


			model.parts.emplace_back();
			ModelC::Part& part = model.parts.back();

			btIndexedMesh mesh;
			{

				auto& acc = m.accessors[primitive.indices];
				auto& view = m.bufferViews[acc.bufferView];
				auto& buffer = m.buffers[view.buffer];
				part.index_data.resize(acc.count);
				unsigned char* data = reinterpret_cast<unsigned char*>(buffer.data.data());
				for (int i = 0; i < acc.count; i++) {
					uint16_t* index = reinterpret_cast<uint16_t*>(data + acc.byteOffset + view.byteOffset + i * (acc.ByteStride(view)));
					part.index_data[i] = *index;
				}

				mesh.m_numTriangles = (int) (acc.count / 3);
				mesh.m_triangleIndexBase = (const unsigned char*) part.index_data.data();
				mesh.m_triangleIndexStride = 3 * sizeof(int);

			}

			{

				auto it = primitive.attributes.find("POSITION");
				if (it == primitive.attributes.end())
					Util::log(Util::warning) << "GLTFLoader : Position needed\n";
				auto& acc = m.accessors[it->second];
				auto& view = m.bufferViews[acc.bufferView];
				auto& buffer = m.buffers[view.buffer];
				part.vertex_data.resize(acc.count*3);
				unsigned char* data = reinterpret_cast<unsigned char*>(buffer.data.data());
				for (int i = 0; i < acc.count; i++) {
					float* vertex = reinterpret_cast<float*>(data + acc.byteOffset + view.byteOffset + i * (acc.ByteStride(view)));
					part.vertex_data[i * 3 + 0] = (*(vertex + 0));
					part.vertex_data[i * 3 + 1] = (*(vertex + 1));
					part.vertex_data[i * 3 + 2] = (*(vertex + 2));
				}

				mesh.m_numVertices = (int) acc.count;
				mesh.m_vertexBase = (const unsigned char*) part.vertex_data.data();
				mesh.m_vertexStride = 3 * sizeof(btScalar);

			}

			model.tivma->addIndexedMesh(mesh);

			
			
			part.material = materials[primitive.material];

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
					Util::log(Util::warning) << "GLTFLoader : Normal needed\n";
				makePart(m, it->second, part.normal, buffers, 1);
			}

			{
				auto it = primitive.attributes.find("TEXCOORD_0");
				if(it == primitive.attributes.end())
					Util::log(Util::warning) << "GLTFLoader : UV needed\n";
				makePart(m, it->second, part.uv, buffers, 2);
			}

			


		}
	}

	btGImpactMeshShape* gimpact = new btGImpactMeshShape(model.tivma.get());
	gimpact->updateBound();
	model.shape = std::unique_ptr<btCollisionShape>(gimpact);

	return model_entity;

}

void GLTFLoader::makePart(Model& m, int index, ModelC::Part::View& v, std::vector<std::shared_ptr<BufferC>>& buffers, int type) {
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