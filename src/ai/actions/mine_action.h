#ifndef MINE_ACTION_H
#define MINE_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <glm/glm.hpp>

namespace dy {

class MineAction : public ActionBase<MineAction> {
public:
    MineAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    void find();

    glm::vec2 stone_tile_{0, 0};
    ActionStageMachine machine_;
};

}

#endif
