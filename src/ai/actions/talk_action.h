#ifndef TALK_ACTION_H
#define TALK_ACTION_H

#include <ai/action_base.h>
#include <ai/action_stage.h>
#include <ai/conversation/conversation.h>

namespace dy {

class LLMManager;

class TalkAction : public ActionBase<TalkAction> {
public:
    TalkAction(entt::registry& reg, entt::entity entity, const ActionParams& params = {});
    ~TalkAction();

    std::unique_ptr<Action> act_impl(std::unique_ptr<Action> self, double dt) override;
    std::string describe() const override;

private:
    entt::entity target_        = entt::null;
    Conversation* conv_         = nullptr;
    ActionStageMachine machine_;
    bool marked_responder_      = false;    // whether we set ConversationResponder on target_

    // ── Dialogue loop state ──────────────────────────────────────────────────
    enum class ExchangePhase {
        SendingInitial,   // sending first message (from params.message)
        WaitingForReply,  // waiting for target's LLM reply
        RequestingNext,   // submitting LLM for initiator's next line
        WaitingForNext,   // waiting for initiator's next LLM line
    };
    ExchangePhase exchange_phase_       = ExchangePhase::SendingInitial;
    int           exchange_count_       = 0;
    static constexpr int max_exchanges_ = 8;

    uint64_t reply_request_id_          = 0;   // target's reply LLM request
    uint64_t next_request_id_           = 0;   // initiator's next line LLM request
    uint64_t summary_request_id_        = 0;   // summary LLM request
    bool     summary_submitted_         = false;
    float    request_elapsed_           = 0.0f;
    static constexpr float request_timeout_ = 15.0f;

    void     send_message(entt::entity speaker, const std::string& text);
    uint64_t submit_dialogue_request(entt::entity speaker, entt::entity listener, LLMManager* llm);
    void     cleanup_responder();
    void     cleanup_and_conclude();
};

} // namespace dy

#endif
