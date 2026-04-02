#include "talk_action.h"

#include <extra/optick/optick.h>

#include <logic/map/map_manager.h>
#include <logic/components/path.h>
#include <logic/components/object.h>
#include <logic/components/basic_needs.h>
#include <ai/aic.h>
#include <ai/conversation/conversation_manager.h>
#include <ai/memory/ai_memory.h>
#include <ai/identity/entity_identity.h>
#include <ai/speech/speech_bubble.h>
#include <llm/llm_manager.h>
#include <llm/prompt_builder.h>

#include <sstream>
#include <glm/glm.hpp>

using namespace dy;

// ── Static helpers ────────────────────────────────────────────────────────────

static entt::entity resolve_target(entt::registry& reg, const std::string& target_name) {
    if (target_name.starts_with("Entity#")) {
        auto id = static_cast<entt::entity>(std::stoul(target_name.substr(7)));
        if (reg.valid(id)) return id;
    }
    for (auto [e, identity] : reg.view<EntityIdentity>().each()) {
        if (identity.ready && identity.name == target_name) return e;
    }
    return entt::null;
}

static std::string entity_display_name(entt::registry& reg, entt::entity e) {
    if (reg.all_of<EntityIdentity>(e)) {
        const auto& eid = reg.get<EntityIdentity>(e);
        if (eid.ready) return eid.name;
    }
    return "Entity#" + std::to_string(static_cast<uint32_t>(e));
}

static LLMManager* get_llm(entt::registry& reg) {
    auto** pp = reg.try_ctx<LLMManager*>();
    return pp ? *pp : nullptr;
}

// ── Member helpers ────────────────────────────────────────────────────────────

void TalkAction::send_message(entt::entity speaker, const std::string& text) {
    auto& conv_mgr = reg.ctx<ConversationManager>();
    std::string name = entity_display_name(reg, speaker);
    conv_mgr.add_message(conv_, speaker, text, name);
    reg.emplace_or_replace<SpeechBubble>(speaker, SpeechBubble{.dialogue = text});
}

uint64_t TalkAction::submit_dialogue_request(entt::entity speaker, entt::entity listener,
                                              LLMManager* llm) {
    std::string speaker_name = entity_display_name(reg, speaker);
    std::string partner_name = entity_display_name(reg, listener);

    std::string personality_type, personality_desc;
    if (reg.all_of<EntityIdentity>(speaker)) {
        const auto& eid = reg.get<EntityIdentity>(speaker);
        if (eid.ready) {
            personality_type = eid.personality_type;
            personality_desc = eid.personality_description;
        }
    }

    PromptBuilder pb;
    std::string user_prompt   = pb.build_dialogue_prompt(personality_type, personality_desc,
                                                          speaker_name, partner_name,
                                                          conv_->messages);
    std::string system_prompt = pb.build_system_prompt(personality_type, personality_desc);
    return llm->submit_request(user_prompt, system_prompt);
}

void TalkAction::cleanup_responder() {
    if (marked_responder_ && reg.valid(target_) && reg.all_of<ConversationResponder>(target_)) {
        const auto& cr = reg.get<ConversationResponder>(target_);
        if (cr.initiator == entity) {
            reg.remove<ConversationResponder>(target_);
        }
    }
    marked_responder_ = false;
}

void TalkAction::cleanup_and_conclude() {
    cleanup_responder();
    if (conv_ && conv_->is_active()) {
        if (auto* conv_mgr = reg.try_ctx<ConversationManager>()) {
            conv_mgr->conclude_conversation(conv_);
        }
    }
}

TalkAction::~TalkAction() {
    cleanup_and_conclude();
}

// ── Constructor ───────────────────────────────────────────────────────────────

TalkAction::TalkAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        // ── 1. Resolve target + pathfind ─────────────────────────────────────
        .stage("finding partner", [this](double) -> StageStatus {
            // Can't initiate if we're already a passive responder
            if (this->reg.all_of<ConversationResponder>(this->entity)) {
                this->failure_reason = "already participating as a responder";
                return StageStatus::Failed;
            }

            if (this->reg.all_of<Path>(this->entity))
                this->reg.remove<Path>(this->entity);

            if (this->params.target_entity != entt::null
                && this->reg.valid(this->params.target_entity)) {
                target_ = this->params.target_entity;
            } else if (!this->params.target_name.empty()) {
                target_ = resolve_target(this->reg, this->params.target_name);
            } else {
                // Nearest human fallback
                auto my_pos = this->reg.get<Position>(this->entity);
                float closest = 999999.0f;
                for (auto [e, obj, pos] : this->reg.view<Object, Position>().each()) {
                    if (e == this->entity || obj.id != Object::human) continue;
                    float d = glm::distance(glm::vec2(my_pos.x, my_pos.y),
                                            glm::vec2(pos.x, pos.y));
                    if (d < closest) { closest = d; target_ = e; }
                }
            }

            if (target_ == entt::null || !this->reg.valid(target_)) {
                this->failure_reason = "no one to talk to";
                return StageStatus::Failed;
            }

            // Don't approach a target already claimed by another conversation
            if (this->reg.all_of<ConversationResponder>(target_)) {
                this->failure_reason = "target is already in a conversation";
                return StageStatus::Failed;
            }

            auto& map     = this->reg.ctx<MapManager>();
            auto  my_pos  = this->reg.get<Position>(this->entity);
            auto  tgt_pos = this->reg.get<Position>(target_);
            auto  tiles   = map.pathfind(my_pos, [&](glm::vec2 p) {
                return glm::distance(p, glm::vec2(tgt_pos.x, tgt_pos.y)) < 3.0f;
            });
            if (tiles.empty()) {
                this->failure_reason = "can't reach conversation partner";
                return StageStatus::Failed;
            }
            this->reg.emplace<Path>(this->entity, std::move(tiles));
            return StageStatus::Complete;
        })

        // ── 2. Walk to target (only the initiator moves) ─────────────────────
        .stage("approaching",
            stage_primitives::wait_until_removed<Path>(reg, entity))

        // ── 3. Open / join conversation and claim responder role ─────────────
        .stage("starting conversation", [this](double) -> StageStatus {
            // Abort if we were claimed as a responder while walking
            if (this->reg.all_of<ConversationResponder>(this->entity)) {
                return StageStatus::Failed;
            }
            // Abort if target was claimed while we were walking
            if (this->reg.all_of<ConversationResponder>(target_)) {
                this->failure_reason = "target was claimed by another conversation";
                return StageStatus::Failed;
            }

            auto& conv_mgr = this->reg.ctx<ConversationManager>();
            conv_ = conv_mgr.find_conversation(this->entity, target_);
            if (!conv_) {
                conv_ = conv_mgr.start_conversation(this->entity, target_);

                // Mark target as passive responder — only we pathfind and drive dialogue
                this->reg.emplace_or_replace<ConversationResponder>(target_,
                    ConversationResponder{.initiator = this->entity});
                marked_responder_ = true;

                // Stop target's current action so they stand still during conversation
                if (auto* aic = this->reg.try_get<AIC>(target_)) {
                    aic->action = nullptr;
                    while (!aic->action_queue.empty()) aic->action_queue.pop();
                    aic->waiting_for_llm        = false;
                    aic->pending_llm_request_id = 0;
                }
            }

            if (!conv_) {
                this->failure_reason = "couldn't start conversation";
                return StageStatus::Failed;
            }
            return StageStatus::Complete;
        })

        // ── 4. Full dialogue loop ─────────────────────────────────────────────
        //
        // Initiator sends → target replies → initiator replies → ...
        // Ends when: LLM signals done, max_exchanges reached, or entity urgency high.
        .stage("conversing", [this](double dt) -> StageStatus {
            if (!conv_ || !conv_->is_active()) return StageStatus::Complete;

            // Urgency check — too hungry to continue talking
            if (this->reg.all_of<BasicNeeds>(this->entity)) {
                if (this->reg.get<BasicNeeds>(this->entity).hunger > 8.0f)
                    return StageStatus::Complete;
            }

            LLMManager* llm = get_llm(this->reg);

            switch (exchange_phase_) {

            // ── Send initial message ──────────────────────────────────────────
            case ExchangePhase::SendingInitial: {
                std::string msg = this->params.message.empty() ? "Hello!" : this->params.message;
                send_message(this->entity, msg);

                if (llm && llm->is_available()) {
                    reply_request_id_ = submit_dialogue_request(target_, this->entity, llm);
                    request_elapsed_  = 0.0f;
                    exchange_phase_   = ExchangePhase::WaitingForReply;
                } else {
                    return StageStatus::Complete;  // no LLM — one message only
                }
                return StageStatus::Continue;
            }

            // ── Wait for target's reply ───────────────────────────────────────
            case ExchangePhase::WaitingForReply: {
                request_elapsed_ += static_cast<float>(dt);
                if (request_elapsed_ > request_timeout_) {
                    reply_request_id_ = 0;
                    return StageStatus::Complete;
                }
                if (!llm) return StageStatus::Complete;

                LLMResponse resp;
                if (!llm->try_get_result(reply_request_id_, resp))
                    return StageStatus::Continue;

                reply_request_id_ = 0;
                exchange_count_++;

                std::string reply = resp.raw_text;
                bool done = false;
                if (resp.parsed_json.is_object()) {
                    reply = resp.parsed_json.value("reply", resp.raw_text);
                    done  = resp.parsed_json.value("done",  false);
                }

                if (!reply.empty()) send_message(target_, reply);

                if (done || exchange_count_ >= max_exchanges_)
                    return StageStatus::Complete;

                exchange_phase_ = ExchangePhase::RequestingNext;
                return StageStatus::Continue;
            }

            // ── Submit LLM for initiator's next line ──────────────────────────
            case ExchangePhase::RequestingNext: {
                if (!llm || !llm->is_available()) return StageStatus::Complete;
                next_request_id_ = submit_dialogue_request(this->entity, target_, llm);
                request_elapsed_ = 0.0f;
                exchange_phase_  = ExchangePhase::WaitingForNext;
                return StageStatus::Continue;
            }

            // ── Wait for initiator's next line ────────────────────────────────
            case ExchangePhase::WaitingForNext: {
                request_elapsed_ += static_cast<float>(dt);
                if (request_elapsed_ > request_timeout_) {
                    next_request_id_ = 0;
                    return StageStatus::Complete;
                }
                if (!llm) return StageStatus::Complete;

                LLMResponse resp;
                if (!llm->try_get_result(next_request_id_, resp))
                    return StageStatus::Continue;

                next_request_id_ = 0;

                std::string next_line = resp.raw_text;
                bool done = false;
                if (resp.parsed_json.is_object()) {
                    next_line = resp.parsed_json.value("reply", resp.raw_text);
                    done      = resp.parsed_json.value("done",  false);
                }

                if (!next_line.empty()) send_message(this->entity, next_line);

                if (done) return StageStatus::Complete;

                // Submit target's reply and loop back
                reply_request_id_ = submit_dialogue_request(target_, this->entity, llm);
                request_elapsed_  = 0.0f;
                exchange_phase_   = ExchangePhase::WaitingForReply;
                return StageStatus::Continue;
            }

            } // switch
            return StageStatus::Complete;
        })

        // ── 5. Conclude: summarize conversation, store in both memories ───────
        .stage("concluding", [this](double dt) -> StageStatus {
            LLMManager* llm = get_llm(this->reg);

            if (!summary_submitted_) {
                summary_submitted_ = true;
                request_elapsed_   = 0.0f;

                bool can_summarize = llm && llm->is_available()
                    && conv_ && conv_->messages.size() >= 2;

                if (can_summarize) {
                    std::string name_a = entity_display_name(this->reg, this->entity);
                    std::string name_b = entity_display_name(this->reg, target_);
                    PromptBuilder pb;
                    std::string prompt = pb.build_summary_prompt(name_a, name_b, conv_->messages);
                    const std::string sys =
                        "You are a neutral narrator. "
                        "Respond only with a valid JSON object, no markdown fences.";
                    summary_request_id_ = llm->submit_request(prompt, sys);
                } else {
                    cleanup_and_conclude();
                    return StageStatus::Complete;
                }
            }

            request_elapsed_ += static_cast<float>(dt);
            if (request_elapsed_ > request_timeout_) {
                cleanup_and_conclude();
                return StageStatus::Complete;
            }
            if (!llm) { cleanup_and_conclude(); return StageStatus::Complete; }

            LLMResponse resp;
            if (!llm->try_get_result(summary_request_id_, resp))
                return StageStatus::Continue;

            summary_request_id_ = 0;

            if (resp.success && resp.parsed_json.is_object()) {
                std::string summary = resp.parsed_json.value("summary", "");
                if (!summary.empty()) {
                    if (auto* mem = this->reg.try_get<AIMemory>(this->entity))
                        mem->add_event("conversation_summary", summary);
                    if (this->reg.valid(target_)) {
                        if (auto* mem = this->reg.try_get<AIMemory>(target_))
                            mem->add_event("conversation_summary", summary);
                    }
                }
            }

            cleanup_and_conclude();
            return StageStatus::Complete;
        });
}

std::unique_ptr<Action> TalkAction::act_impl(std::unique_ptr<Action> self, double dt) {
    OPTICK_EVENT();
    StageStatus status = machine_.tick(dt);
    if (status == StageStatus::Failed) return nullptr;
    return (status == StageStatus::Continue) ? std::move(self) : nullptr;
}

std::string TalkAction::describe() const {
    return "Talk: " + machine_.current_stage_name();
}
