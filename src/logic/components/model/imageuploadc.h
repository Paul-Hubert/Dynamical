#ifndef IMAGEUPLOADC_H
#define IMAGEUPLOADC_H

#include "renderer/vk.h"
#include "renderer/context.h"

class ImageUploadC {
public:
	ImageUploadC(Context& ctx, size_t real_size, vk::ImageCreateInfo info, vk::ImageLayout layout) : real_size(real_size), info(info), layout(layout) {

		VmaAllocationCreateInfo ainfo{};
		ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
		ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
		auto binfo = vk::BufferCreateInfo({}, real_size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i);
		buffer = VmaBuffer(ctx.device, &ainfo, binfo);

		VmaAllocationInfo inf;
		vmaGetAllocationInfo(ctx.device, buffer.allocation, &inf);

		data = inf.pMappedData;

	}

	size_t real_size;
	void* data;
	vk::ImageCreateInfo info;
	vk::ImageLayout layout;
	VmaBuffer buffer;
};

#endif