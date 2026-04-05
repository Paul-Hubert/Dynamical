#ifndef BUILD_ACTION_H
#define BUILD_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <ai/building_placement.h>
#include <logic/components/building.h>

namespace dy {

class BuildAction : public ActionBase<BuildAction> {
public:
    BuildAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    ActionStageMachine machine_;
    PlacementResult placement_result_;
    const BuildingTemplate* building_template_ = nullptr;
    entt::entity building_entity_ = entt::null;

    void cleanup_reserved_tiles();
};

}

#endif
