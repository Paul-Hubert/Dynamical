#ifndef BUFFERUPLOADC_H
#define BUFFERUPLOADC_H

#include "renderer/vk.h"
#include "renderer/context.h"

class BufferUploadC {
public:
	BufferUploadC(Context& ctx, vk::BufferCreateInfo info) : size(info.size), info(info) {

		VmaAllocationCreateInfo ainfo{};
		ainfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
		ainfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
		auto binfo = ctx.device.isDedicated() ? vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 1, &ctx.device.t_i) : info;
		buffer = VmaBuffer(ctx.device, &ainfo, binfo);

		VmaAllocationInfo inf;
		vmaGetAllocationInfo(ctx.device, buffer.allocation, &inf);

		data = inf.pMappedData;

	}
	uint32_t size;
	void* data;
	vk::BufferCreateInfo info;
	VmaBuffer buffer;
};

#endif