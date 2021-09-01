#ifndef AISYS_H
#define AISYS_H

#include "logic/systems/system.h"
#include "aic.h"

namespace dy {

class AISys : public System {
public:
    AISys(entt::registry& reg);
    ~AISys() override;
    const char* name() override {
        return "AI";
    }
    void tick(float dt) override;
private:
    void decide(entt::entity entity, AIC& ai);
    template<typename T>
    void testAction(entt::entity entity, float& max_score, std::unique_ptr<Action>& max_action) {
        float score = T::getScore(reg, entity);
        if(score > max_score) {
            max_score = score;
            max_action = std::make_unique<T>(reg, entity);
        }
    }
};

}

#endif
