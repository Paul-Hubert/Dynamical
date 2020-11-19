#include "uploader.h"

#include "renderer/context.h"

#include "util/util.h"

#include "logic/components/model/bufferuploadc.h"
#include "logic/components/model/imageuploadc.h"

void UploaderSys::preinit() {

}

void UploaderSys::init() {

	Context& ctx = *reg.ctx<Context*>();

	pool = ctx.device->createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eTransient, ctx.device.t_i));

}

void UploaderSys::tick() {

	Context& ctx = *reg.ctx<Context*>();

	for (auto it = uploads.begin(); it != uploads.end();) {
		Upload& upload = *it;
		vk::Result result = ctx.device->waitForFences({ upload.fence }, VK_TRUE, 0);
		if (result == vk::Result::eSuccess) {

			ctx.device->destroy(upload.fence);
			ctx.device->freeCommandBuffers(pool, {upload.command});

			for (auto entity : upload.uploaded) {
				if (reg.has<BufferUploadC>(entity)) {
					reg.remove<BufferUploadC>(entity);
				} else {
					reg.remove<ImageUploadC>(entity);
				}
			}

			it = uploads.erase(it);

		} else {
			++it;
		}
	}

	Upload current{};

	auto bview = reg.view<BufferUploadC>(entt::exclude<VmaBuffer>);
	auto iview = reg.view<ImageUploadC>(entt::exclude<VmaImage>);

	if(!bview.empty() || !iview.empty()) {

		current.fence = ctx.device->createFence({});
		current.command = ctx.device->allocateCommandBuffers(vk::CommandBufferAllocateInfo(pool, vk::CommandBufferLevel::ePrimary, 1))[0];
		current.command.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

		bview.each([&](auto entity, BufferUploadC& upload) {

			if (ctx.device.isDedicated()) {

				current.uploaded.push_back(entity);

				VmaAllocationCreateInfo info{};
				info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
				upload.info.usage |= vk::BufferUsageFlagBits::eTransferDst;
				auto& buffer = reg.emplace<VmaBuffer>(entity, ctx.device, &info, upload.info);

				current.command.copyBuffer(upload.buffer, buffer, { vk::BufferCopy(0, 0, buffer.size) });

			} else {

				reg.emplace<VmaBuffer>(entity, std::move(upload.buffer));

				reg.remove<BufferUploadC>(entity);

			}

		});

		iview.each([&](auto entity, ImageUploadC& upload) {

			current.uploaded.push_back(entity);
			VmaAllocationCreateInfo info{};
			info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			auto& image = reg.emplace<VmaImage>(entity, ctx.device, &info, upload.info);

			vk::BufferImageCopy bic(0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1), vk::Offset3D(0, 0, 0), upload.info.extent);
			current.command.copyBufferToImage(upload.buffer, image, upload.layout, 1, &bic);

		});

		current.command.end();

		ctx.device.transfer.submit({vk::SubmitInfo(0, nullptr, nullptr, 1, &current.command, 0 , nullptr)}, current.fence);

		uploads.push_back(current);

	}

}

void UploaderSys::finish() {

	Context& ctx = *reg.ctx<Context*>();
	ctx.device->destroy(pool);

}