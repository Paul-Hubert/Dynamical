#ifndef RENDERABLEC_H
#define RENDERABLEC_H

class RenderableC {
public:

	RenderableC(entt::entity model) : model(model) {}
	entt::entity model;
};

#endif