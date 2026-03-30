#include <gtest/gtest.h>
#include <llm/response_parser.h>
#include <fixtures/test_fixtures.h>

class ResponseParserTest : public Phase3TestFixture {
protected:
    ResponseParser parser;
};

// Test valid responses with all action types
TEST_F(ResponseParserTest, ParseEatAction) {
    auto response = MockLLMProvider::create_eat_response();
    auto result = parser.parse(response);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.thought, "I am hungry, let me eat some berries");
    EXPECT_EQ(result.dialogue, "Time for lunch!");
    EXPECT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].action_id, ActionID::Eat);
    EXPECT_EQ(result.actions[0].params.resource, "berry");
    EXPECT_EQ(result.actions[0].params.amount, 3);
}

TEST_F(ResponseParserTest, ParseWanderAction) {
    auto response = MockLLMProvider::create_wander_response();
    auto result = parser.parse(response);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].action_id, ActionID::Wander);
    EXPECT_EQ(result.actions[0].params.direction, "north");
}

TEST_F(ResponseParserTest, ParseHarvestAction) {
    auto response = MockLLMProvider::create_harvest_response();
    auto result = parser.parse(response);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].action_id, ActionID::Harvest);
    EXPECT_EQ(result.actions[0].params.target_type, "berry_bush");
    EXPECT_EQ(result.actions[0].params.resource, "berry");
    EXPECT_EQ(result.actions[0].params.amount, 5);
}

TEST_F(ResponseParserTest, ParseTradeAction) {
    auto response = MockLLMProvider::create_trade_response();
    auto result = parser.parse(response);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].action_id, ActionID::Trade);
    EXPECT_EQ(result.actions[0].params.target_name, "Trader");
    EXPECT_EQ(result.actions[0].params.trade_offer.give_item, "berry");
    EXPECT_EQ(result.actions[0].params.trade_offer.give_amount, 5);
    EXPECT_EQ(result.actions[0].params.trade_offer.want_item, "wood");
    EXPECT_EQ(result.actions[0].params.trade_offer.want_amount, 2);
}

TEST_F(ResponseParserTest, ParseMultipleActions) {
    auto response = MockLLMProvider::create_multiple_actions();
    auto result = parser.parse(response);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.actions.size(), 3);

    // First action: Harvest
    EXPECT_EQ(result.actions[0].action_id, ActionID::Harvest);
    EXPECT_EQ(result.actions[0].params.resource, "berry");

    // Second action: Eat
    EXPECT_EQ(result.actions[1].action_id, ActionID::Eat);
    EXPECT_EQ(result.actions[1].params.amount, 2);

    // Third action: Talk
    EXPECT_EQ(result.actions[2].action_id, ActionID::Talk);
    EXPECT_EQ(result.actions[2].params.message, "Hello friend!");
}

// Test error handling
TEST_F(ResponseParserTest, HandleMissingActionsField) {
    auto response = MockLLMProvider::create_invalid_json();
    auto result = parser.parse(response);

    // Should fail gracefully
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.actions.size(), 0);
}

TEST_F(ResponseParserTest, HandleEmptyActionsArray) {
    auto response = MockLLMProvider::create_empty_actions();
    auto result = parser.parse(response);

    // Empty actions should result in false success
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.actions.size(), 0);
}

TEST_F(ResponseParserTest, HandleInvalidActionName) {
    auto response = MockLLMProvider::create_invalid_action_name();
    auto result = parser.parse(response);

    // Invalid action names should be dropped
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.actions.size(), 0);
}

TEST_F(ResponseParserTest, ParseAllActionTypes) {
    // Test that all 13 action types are recognized
    std::vector<std::string> action_names = {
        "Wander", "Eat", "Harvest", "Mine", "Hunt", "Build", "Sleep",
        "Trade", "Talk", "Craft", "Fish", "Explore", "Flee"
    };

    for (const auto& action_name : action_names) {
        json response = {
            {"thought", "Testing " + action_name},
            {"actions", json::array({
                json{
                    {"action", action_name},
                    {"target", "test_target"}
                }
            })},
            {"dialogue", "Testing"}
        };

        auto result = parser.parse(response);
        EXPECT_TRUE(result.success) << "Failed to parse action: " << action_name;
        EXPECT_EQ(result.actions.size(), 1) << "Expected 1 action for: " << action_name;
        EXPECT_NE(result.actions[0].action_id, ActionID::None) << "Action ID should not be None for: " << action_name;
    }
}

TEST_F(ResponseParserTest, ParseWithMissingOptionalFields) {
    json response = {
        {"thought", "Minimal response"},
        {"actions", json::array({
            json{
                {"action", "Wander"}
                // No other fields
            }
        })}
        // No dialogue field
    };

    auto result = parser.parse(response);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.actions.size(), 1);
    EXPECT_EQ(result.actions[0].action_id, ActionID::Wander);
    EXPECT_EQ(result.dialogue, ""); // Should be empty string
}

TEST_F(ResponseParserTest, ParameterExtraction) {
    json response = {
        {"thought", "Comprehensive action"},
        {"actions", json::array({
            json{
                {"action", "Harvest"},
                {"target", "oak_tree"},
                {"target_name", "Big Oak"},
                {"resource", "wood"},
                {"amount", 10},
                {"recipe", "wooden_planks"},
                {"structure", "house"},
                {"direction", "east"},
                {"duration", 5.5f}
            }
        })},
        {"dialogue", "Harvesting"}
    };

    auto result = parser.parse(response);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.actions[0].params.target_type, "oak_tree");
    EXPECT_EQ(result.actions[0].params.target_name, "Big Oak");
    EXPECT_EQ(result.actions[0].params.resource, "wood");
    EXPECT_EQ(result.actions[0].params.amount, 10);
    EXPECT_EQ(result.actions[0].params.recipe, "wooden_planks");
    EXPECT_EQ(result.actions[0].params.structure, "house");
    EXPECT_EQ(result.actions[0].params.direction, "east");
    EXPECT_FLOAT_EQ(result.actions[0].params.duration, 5.5f);
}

TEST_F(ResponseParserTest, MalformedJSON) {
    // Test with completely invalid JSON structure
    json response;
    response["invalid"] = "structure";
    response["missing"] = "required_fields";

    auto result = parser.parse(response);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.actions.size(), 0);
}

TEST_F(ResponseParserTest, ActionsNotArray) {
    json response = {
        {"thought", "Bad structure"},
        {"actions", "should_be_array"},  // This is a string, not array
        {"dialogue", "Error"}
    };

    auto result = parser.parse(response);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.actions.size(), 0);
}
