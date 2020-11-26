#include "factory_list.h"

#include "logic/components/model/renderablec.h"

entt::entity ObjectFactory::build(entt::registry& reg, entt::entity model) {
	entt::entity box = reg.create();

	reg.emplace<RenderableC>(box, model);

	return box;
}