#ifndef RENDERABLEC_H
#define RENDERABLEC_H

#include "entt/entt.hpp"

class RenderableC {
public:
	RenderableC(entt::entity model) : model(model) {}
	entt::entity model;
};

#endif