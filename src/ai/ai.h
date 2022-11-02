#ifndef AISYS_H
#define AISYS_H

#include "logic/systems/system.h"
#include "aic.h"

#include "behaviors/behavior.h"

namespace dy {

class AISys : public System {
public:
    AISys(entt::registry& reg);
    ~AISys() override;
    const char* name() override {
        return "AI";
    }
    void tick(double dt) override;
private:
    void decide(entt::entity entity, AIC& ai);
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
