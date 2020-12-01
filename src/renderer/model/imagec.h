#ifndef TEXTUREC_H
#define TEXTUREC_H

#include "renderer/util/vmapp.h"

class ImageC {
public:
	template<typename... Args>
	ImageC(Args &&... args) : image(std::forward<Args>(args)...) {}
	VmaImage image;
	bool ready = false;
};

#endif