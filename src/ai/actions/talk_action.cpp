#include "talk_action.h"

#include <extra/optick/optick.h>

#include <logic/map/map_manager.h>
#include <logic/components/path.h>
#include <logic/components/object.h>
#include <ai/conversation/conversation_manager.h>
#include <ai/memory/ai_memory.h>
#include <ai/identity/entity_identity.h>
#include <ai/speech/speech_bubble.h>
#include <llm/llm_manager.h>
#include <llm/prompt_builder.h>

#include <sstream>
#include <glm/glm.hpp>

using namespace dy;

// Resolve a target entity by name (EntityIdentity.name) or raw "Entity#id" syntax
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

static std::string build_reply_prompt(entt::registry& reg, entt::entity responder,
                                      const std::string& other_name,
                                      const std::string& last_message) {
    std::ostringstream oss;
    oss << other_name << " just said to you: \"" << last_message << "\"\n\n";

    if (reg.all_of<AIMemory>(responder)) {
        const auto& mem = reg.get<AIMemory>(responder);
        if (!mem.events.empty()) {
            oss << "Recent memory:\n";
            int shown = 0;
            for (int i = (int)mem.events.size() - 1; i >= 0 && shown < 3; --i, ++shown)
                oss << "- " << mem.events[i].description << "\n";
            oss << "\n";
        }
    }

    oss << "Respond naturally, in character. Return JSON:\n"
        << "{ \"thought\": \"your internal reaction\", \"reply\": \"what you say\" }\n";
    return oss.str();
}

// Helper: get entity's display name from EntityIdentity, or fall back to "Entity#id"
static std::string entity_display_name(entt::registry& reg, entt::entity e) {
    if (reg.all_of<EntityIdentity>(e)) {
        const auto& eid = reg.get<EntityIdentity>(e);
        if (eid.ready) return eid.name;
    }
    return "Entity#" + std::to_string(static_cast<uint32_t>(e));
}

// Submit an LLM reply request on behalf of `responder`, reacting to `last_msg` from `other`.
static uint64_t submit_reply(entt::registry& reg, LLMManager* llm,
                              entt::entity responder, entt::entity other,
                              const std::string& last_msg) {
    std::string other_name = entity_display_name(reg, other);
    std::string user_prompt = build_reply_prompt(reg, responder, other_name, last_msg);
    std::string system_prompt;
    if (reg.all_of<EntityIdentity>(responder)) {
        const auto& rid = reg.get<EntityIdentity>(responder);
        if (rid.ready) {
            PromptBuilder pb;
            system_prompt = pb.build_system_prompt(rid.personality_type,
                                                   rid.personality_description);
        }
    }
    return llm->submit_request(user_prompt, system_prompt);
}

TalkAction::TalkAction(entt::registry& reg, entt::entity entity, const ActionParams& params)
    : ActionBase(reg, entity, params)
{
    machine_
        // ── 1. Resolve target + pathfind ────────────────────────────────────
        .stage("finding partner", [this](double) -> StageStatus {
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

            if (target_ == entt::null) {
                this->failure_reason = "no one to talk to";
                return StageStatus::Failed;
            }

            auto& map       = this->reg.ctx<MapManager>();
            auto my_pos     = this->reg.get<Position>(this->entity);
            auto target_pos = this->reg.get<Position>(target_);
            auto tiles = map.pathfind(my_pos, [&](glm::vec2 p) {
                return glm::distance(p, glm::vec2(target_pos.x, target_pos.y)) < 3.0f;
            });
            if (tiles.empty()) {
                this->failure_reason = "can't reach conversation partner";
                return StageStatus::Failed;
            }
            this->reg.emplace<Path>(this->entity, std::move(tiles));
            return StageStatus::Complete;
        })

        // ── 2. Walk to target ────────────────────────────────────────────────
        .stage("approaching",
            stage_primitives::wait_until_removed<Path>(reg, entity))

        // ── 3. Open / join conversation ──────────────────────────────────────
        .stage("starting conversation", [this](double) -> StageStatus {
            auto& conv_mgr = this->reg.ctx<ConversationManager>();
            conv_ = conv_mgr.find_conversation(this->entity, target_);
            if (!conv_) conv_ = conv_mgr.start_conversation(this->entity, target_);
            if (!conv_) {
                this->failure_reason = "couldn't start conversation";
                return StageStatus::Failed;
            }
            return StageStatus::Complete;
        })

        // ── 4. Single exchange: speaker sends message, wait for target's reply ─
        //
        // One TalkAction = one exchange. AISys re-runs LLM decision after
        // completion; the entity chooses Talk again to continue or moves on.
        .stage("conversing", [this](double dt) -> StageStatus {
            if (!conv_ || !conv_->is_active()) return StageStatus::Complete;

            auto** llm_pp = this->reg.try_ctx<LLMManager*>();
            LLMManager* llm = llm_pp ? *llm_pp : nullptr;

            // ── Poll pending LLM reply from target ───────────────────────────
            if (response_request_id_ != 0) {
                response_wait_elapsed_ += (float)dt;
                if (response_wait_elapsed_ > response_timeout_) {
                    response_request_id_ = 0;
                    return StageStatus::Complete;
                }

                if (!llm) return StageStatus::Complete;

                LLMResponse resp;
                if (!llm->try_get_result(response_request_id_, resp))
                    return StageStatus::Continue;  // still pending

                response_request_id_ = 0;

                std::string reply = resp.parsed_json.is_object()
                    ? resp.parsed_json.value("reply", resp.raw_text)
                    : resp.raw_text;

                this->reg.emplace_or_replace<SpeechBubble>(target_, SpeechBubble{.dialogue = reply});

                auto& conv_mgr = this->reg.ctx<ConversationManager>();
                std::string target_name = entity_display_name(this->reg, target_);
                conv_mgr.add_message(conv_, target_, reply, target_name);

                if (auto* mem = this->reg.try_get<AIMemory>(target_))
                    mem->add_event("said", "Said: \"" + reply + "\"");
                if (auto* mem = this->reg.try_get<AIMemory>(this->entity))
                    mem->add_event("heard", "Heard: \"" + reply + "\"");

                // Exchange complete — return control to AISys for next decision
                return StageStatus::Complete;
            }

            // ── Send initial message (first call only) ───────────────────────
            if (!exchange_started_) {
                exchange_started_ = true;
                std::string msg = this->params.message.empty() ? "Hello!" : this->params.message;

                auto& conv_mgr = this->reg.ctx<ConversationManager>();
                std::string my_name = entity_display_name(this->reg, this->entity);
                conv_mgr.add_message(conv_, this->entity, msg, my_name);

                this->reg.emplace_or_replace<SpeechBubble>(this->entity, SpeechBubble{.dialogue = msg});

                if (auto* mem = this->reg.try_get<AIMemory>(this->entity))
                    mem->add_event("said", "Said: \"" + msg + "\"");
                if (auto* mem = this->reg.try_get<AIMemory>(target_))
                    mem->add_event("heard", "Heard: \"" + msg + "\"");

                if (llm && llm->is_available()) {
                    response_request_id_ = submit_reply(this->reg, llm, target_, this->entity, msg);
                } else {
                    return StageStatus::Complete;  // no LLM — nothing more to exchange
                }
            }

            return StageStatus::Continue;
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
