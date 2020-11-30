#ifndef MODEL_RENDERABLEC_H
#define MODEL_RENDERABLEC_H

#include "entt/entt.hpp"

class ModelRenderableC {
public:
	ModelRenderableC(entt::entity model) : model(model) {}
	entt::entity model;
};

#endif