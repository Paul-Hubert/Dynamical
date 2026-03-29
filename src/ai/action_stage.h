#ifndef ACTION_STAGE_H
#define ACTION_STAGE_H

#include <functional>
#include <string>
#include <vector>
#include <entt/entt.hpp>

namespace dy {

/// Result returned by each stage tick and by ActionStageMachine::tick()
enum class StageStatus {
    Continue,   ///< Stage is still running (or advanced but next stage starts next tick)
    Complete,   ///< All stages finished — action should complete
    Failed      ///< Stage failed — set failure_reason on Action before returning
};

/// A single named stage in an action's lifecycle.
/// The fn lambda captures `this` (the action) for access to reg, entity, and action members.
struct ActionStage {
    std::string name;
    std::function<StageStatus(double dt)> fn;
};

/// Lightweight sequential stage machine for multi-phase actions.
///
/// Stages are registered once (typically in the action constructor) via the builder API.
/// Each tick(), the current stage's fn is called. When a stage returns Complete, the machine
/// advances to the next stage and returns Continue (next stage runs on the NEXT tick).
/// This matches the ECS pattern where components emplaced on one tick are processed by
/// other systems before the next tick.
///
/// Usage:
///   machine_
///       .stage("finding target", [this](double) -> StageStatus { ... })
///       .stage("navigating",     stage_primitives::wait_until_removed<Path>(reg, entity))
///       .stage("working",        stage_primitives::wait_until_removed<Work>(reg, entity));
class ActionStageMachine {
public:
    /// Add a stage at the end of the sequence. Returns *this for chaining.
    ActionStageMachine& stage(std::string name, std::function<StageStatus(double)> fn) {
        stages_.push_back({ std::move(name), std::move(fn) });
        return *this;
    }

    /// Tick the current stage.
    /// - If stage returns StageStatus::Complete: advances current_ and returns Continue
    ///   (next stage starts on next tick).
    /// - If all stages complete: returns StageStatus::Complete.
    /// - If stage returns StageStatus::Failed: propagates immediately.
    StageStatus tick(double dt) {
        if (stages_.empty() || current_ >= stages_.size()) {
            return StageStatus::Complete;
        }

        StageStatus result = stages_[current_].fn(dt);

        if (result == StageStatus::Failed) {
            return StageStatus::Failed;
        }

        if (result == StageStatus::Complete) {
            current_++;
            if (current_ >= stages_.size()) {
                return StageStatus::Complete;
            }
            return StageStatus::Continue;  // next stage runs next tick
        }

        return StageStatus::Continue;
    }

    /// Name of the currently executing stage, or "" if finished/empty.
    const std::string& current_stage_name() const {
        static const std::string empty;
        if (current_ >= stages_.size()) return empty;
        return stages_[current_].name;
    }

    /// Reset the machine to the beginning (stage 0).
    void reset() {
        current_ = 0;
    }

private:
    std::vector<ActionStage> stages_;
    size_t current_ = 0;
};


/// Reusable stage lambda factories for common ECS wait patterns.
namespace stage_primitives {

/// Advance when the entity no longer has component C.
/// Use after emplacing a component that another system will remove when done.
template<typename C>
std::function<StageStatus(double)> wait_until_removed(entt::registry& reg, entt::entity entity) {
    return [&reg, entity](double) -> StageStatus {
        return reg.all_of<C>(entity) ? StageStatus::Continue : StageStatus::Complete;
    };
}

/// Advance when the entity gains component C.
template<typename C>
std::function<StageStatus(double)> wait_until_present(entt::registry& reg, entt::entity entity) {
    return [&reg, entity](double) -> StageStatus {
        return reg.all_of<C>(entity) ? StageStatus::Complete : StageStatus::Continue;
    };
}

/// Advance after `seconds` of elapsed dt.
/// Use for Sleep, crafting animations, cooldowns, etc.
inline std::function<StageStatus(double)> wait_for_seconds(double seconds) {
    return [seconds, elapsed = 0.0](double dt) mutable -> StageStatus {
        elapsed += dt;
        return elapsed >= seconds ? StageStatus::Complete : StageStatus::Continue;
    };
}

} // namespace stage_primitives

} // namespace dy

#endif
