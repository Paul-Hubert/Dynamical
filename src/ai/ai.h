#ifndef AISYS_H
#define AISYS_H

#include "logic/systems/system.h"
#include "aic.h"

#include "behaviors/behavior.h"

#include "llm/llm_manager.h"
#include "llm/prompt_builder.h"
#include "llm/response_parser.h"

namespace dy {

class AISys : public System {
public:
    AISys(entt::registry& reg);
    ~AISys() override;
    const char* name() override {
        return "AI";
    }
    void tick(double dt) override;

    void set_llm_manager(LLMManager* mgr) { llm_manager = mgr; }

private:
    LLMManager* llm_manager = nullptr;
    PromptBuilder prompt_builder;
    ResponseParser response_parser;

    void decide(entt::entity entity, AIC& ai);
    void poll_llm_results();
    void submit_llm_request(entt::entity entity, AIC& ai);

    template<typename T>
    void testBehavior(entt::entity entity, float& max_score, std::unique_ptr<Behavior>& max_behavior) {
        float score = T::getScore(reg, entity);
        if(score > max_score) {
            max_score = score;
            max_behavior = std::make_unique<T>(reg, entity);
        }
    }
};

}

#endif
