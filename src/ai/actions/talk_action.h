#ifndef TALK_ACTION_H
#define TALK_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <ai/conversation/conversation.h>

namespace dy {

class TalkAction : public ActionBase<TalkAction> {
public:
    TalkAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    entt::entity target_            = entt::null;
    Conversation* conv_             = nullptr;
    ActionStageMachine machine_;

    bool         exchange_started_  = false;        // initial message sent
    uint64_t     response_request_id_   = 0;
    float        response_wait_elapsed_ = 0.0f;
    static constexpr float response_timeout_ = 15.0f;
};

}

#endif
