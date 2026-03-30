#pragma once

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <entt/entt.hpp>
#include <string>
#include <memory>

// Phase 2 components
#include <ai/personality/personality.h>
#include <ai/memory/ai_memory.h>
#include <ai/action_registry.h>

using json = nlohmann::json;
using namespace dy;

/// Mock LLM Provider for testing
class MockLLMProvider {
public:
    /// Return a hardcoded LLM response for testing
    static json create_eat_response() {
        return json{
            {"thought", "I am hungry, let me eat some berries"},
            {"actions", json::array({
                json{
                    {"action", "Eat"},
                    {"resource", "berry"},
                    {"amount", 3}
                }
            })},
            {"dialogue", "Time for lunch!"}
        };
    }

    static json create_wander_response() {
        return json{
            {"thought", "I should explore the area"},
            {"actions", json::array({
                json{
                    {"action", "Wander"},
                    {"direction", "north"}
                }
            })},
            {"dialogue", "Let's see what's over there"}
        };
    }

    static json create_harvest_response() {
        return json{
            {"thought", "There are berries nearby"},
            {"actions", json::array({
                json{
                    {"action", "Harvest"},
                    {"target", "berry_bush"},
                    {"resource", "berry"},
                    {"amount", 5}
                }
            })},
            {"dialogue", "Great harvest!"}
        };
    }

    static json create_trade_response() {
        return json{
            {"thought", "I could trade some berries for wood"},
            {"actions", json::array({
                json{
                    {"action", "Trade"},
                    {"target_name", "Trader"},
                    {"offer", json{
                        {"give", "berry"},
                        {"give_amount", 5},
                        {"want", "wood"},
                        {"want_amount", 2}
                    }}
                }
            })},
            {"dialogue", "Would you trade?"}
        };
    }

    static json create_invalid_json() {
        return json{
            {"thought", "Invalid response"},
            // Missing "actions" field
            {"dialogue", "No actions"}
        };
    }

    static json create_empty_actions() {
        return json{
            {"thought", "Empty action list"},
            {"actions", json::array()},
            {"dialogue", "Nothing to do"}
        };
    }

    static json create_invalid_action_name() {
        return json{
            {"thought", "Invalid action"},
            {"actions", json::array({
                json{
                    {"action", "InvalidActionName"},
                    {"target", "something"}
                }
            })},
            {"dialogue", "Trying invalid action"}
        };
    }

    static json create_multiple_actions() {
        return json{
            {"thought", "Multiple actions needed"},
            {"actions", json::array({
                json{
                    {"action", "Harvest"},
                    {"target", "berry_bush"},
                    {"resource", "berry"},
                    {"amount", 5}
                },
                json{
                    {"action", "Eat"},
                    {"resource", "berry"},
                    {"amount", 2}
                },
                json{
                    {"action", "Talk"},
                    {"target_name", "Friend"},
                    {"message", "Hello friend!"}
                }
            })},
            {"dialogue", "Busy day ahead"}
        };
    }
};

/// Test fixture base class
class Phase3TestFixture : public ::testing::Test {
protected:
    Phase3TestFixture() = default;
    virtual ~Phase3TestFixture() = default;

    static void SetUpTestSuite() {
        dy::ActionRegistry::instance().initialize_descriptors();
    }

    /// Create a test registry
    entt::registry& create_test_registry() {
        static entt::registry test_registry;
        return test_registry;
    }

    /// Create a test entity with Personality and AIMemory
    entt::entity create_test_entity(const std::string& name = "TestEntity") {
        auto& reg = create_test_registry();
        entt::entity entity = reg.create();

        // Create and assign Personality
        Personality personality;
        personality.archetype = "explorer";
        personality.speech_style = "casual";
        personality.motivation = "knowledge";
        personality.personality_seed = 12345;
        personality.traits = {
            {"curiosity", 0.8f, "Very curious about the world"},
            {"courage", 0.7f, "Brave and willing to take risks"},
            {"friendliness", 0.6f, "Gets along with others"}
        };
        reg.emplace<Personality>(entity, personality);

        // Create and assign AIMemory
        AIMemory memory;
        memory.add_event("spawned", "Spawned at the starting location");
        memory.add_event("harvested", "Collected 5 berries from bush");
        memory.add_event("talked", "Had a conversation with another entity");
        reg.emplace<AIMemory>(entity, memory);

        return entity;
    }

    /// Get personality from entity
    Personality* get_personality(entt::registry& reg, entt::entity entity) {
        if (reg.all_of<Personality>(entity)) {
            return &reg.get<Personality>(entity);
        }
        return nullptr;
    }

    /// Get memory from entity
    AIMemory* get_memory(entt::registry& reg, entt::entity entity) {
        if (reg.all_of<AIMemory>(entity)) {
            return &reg.get<AIMemory>(entity);
        }
        return nullptr;
    }
};
