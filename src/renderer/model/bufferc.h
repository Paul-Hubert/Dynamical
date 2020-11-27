#ifndef BUFFERC_H
#define BUFFERC_H

#include "renderer/vmapp.h"

class BufferC {
public:
	template<typename... Args>
	BufferC(Args &&... args) : buffer(std::forward<Args>(args)...) {}

	VmaBuffer buffer;
	bool ready = false;
};

#endif