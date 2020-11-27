#ifndef RENDERABLEC_H
#define RENDERABLEC_H

#include <memory>

#include "renderer/model/modelc.h"

class RenderableC {
public:
	RenderableC(std::shared_ptr<ModelC> model) : model(model) {}
	std::shared_ptr<ModelC> model;
};

#endif