#include <gtest/gtest.h>
#include <llm/prompt_builder.h>
#include <fixtures/test_fixtures.h>

class PromptBuilderTest : public Phase3TestFixture {
protected:
    PromptBuilder builder;
};

TEST_F(PromptBuilderTest, BuildContextFromEntity) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");

    // Build context
    PromptContext ctx = build_context(reg, entity, "Alice");

    EXPECT_EQ(ctx.entity_name, "Alice");
    EXPECT_EQ(ctx.entity_id, entity);
    EXPECT_NE(ctx.personality, nullptr);
    EXPECT_NE(ctx.memory, nullptr);
}

TEST_F(PromptBuilderTest, BuildContextWithoutPersonality) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx = build_context(reg, entity, "Bob");

    EXPECT_EQ(ctx.entity_name, "Bob");
    EXPECT_EQ(ctx.personality, nullptr);
}

TEST_F(PromptBuilderTest, BuildContextWithoutMemory) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    // Add personality but not memory
    Personality personality;
    personality.archetype = "builder";
    reg.emplace<Personality>(entity, personality);

    PromptContext ctx = build_context(reg, entity, "Charlie");

    EXPECT_NE(ctx.personality, nullptr);
    EXPECT_EQ(ctx.memory, nullptr);
}

TEST_F(PromptBuilderTest, BuildSystemPrompt) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");
    Personality* personality = get_personality(reg, entity);

    std::string system_prompt = builder.build_system_prompt(personality);

    EXPECT_FALSE(system_prompt.empty());
    EXPECT_NE(system_prompt.find("explorer"), std::string::npos);  // archetype
    EXPECT_NE(system_prompt.find("casual"), std::string::npos);   // speech style
    EXPECT_NE(system_prompt.find("knowledge"), std::string::npos); // motivation
}

TEST_F(PromptBuilderTest, BuildSystemPromptNull) {
    std::string system_prompt = builder.build_system_prompt(nullptr);

    EXPECT_FALSE(system_prompt.empty());
    EXPECT_NE(system_prompt.find("decision"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildDecisionPrompt) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");
    Personality* personality = get_personality(reg, entity);
    AIMemory* memory = get_memory(reg, entity);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = personality;
    ctx.memory = memory;
    ctx.hunger = 5.0f;
    ctx.energy = 7.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_FALSE(prompt.empty());
    EXPECT_NE(prompt.find("Alice"), std::string::npos);
    EXPECT_NE(prompt.find("explorer"), std::string::npos);
    EXPECT_NE(prompt.find("Hunger"), std::string::npos);
    EXPECT_NE(prompt.find("Energy"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsPersonalityTraits) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");
    Personality* personality = get_personality(reg, entity);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = personality;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    // Should contain trait names
    EXPECT_NE(prompt.find("curiosity"), std::string::npos);
    EXPECT_NE(prompt.find("courage"), std::string::npos);
    EXPECT_NE(prompt.find("friendliness"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsMemoryEvents) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");
    AIMemory* memory = get_memory(reg, entity);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = memory;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    // Should contain memory events
    EXPECT_NE(prompt.find("spawned"), std::string::npos);
    EXPECT_NE(prompt.find("harvested"), std::string::npos);
    EXPECT_NE(prompt.find("talked"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsStateSection) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "TestEntity";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 3.5f;
    ctx.energy = 8.2f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("Hunger"), std::string::npos);
    EXPECT_NE(prompt.find("Energy"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsNearbyEntities) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;
    ctx.nearby_entities = R"([{"name": "Bob", "distance": 10}, {"name": "Charlie", "distance": 20}])";

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("NEARBY ENTITIES"), std::string::npos);
    EXPECT_NE(prompt.find("Bob"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsNearbyResources) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;
    ctx.nearby_resources = R"([{"type": "berry_bush", "distance": 5}, {"type": "stone", "distance": 15}])";

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("NEARBY RESOURCES"), std::string::npos);
    EXPECT_NE(prompt.find("berry_bush"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsAvailableActions) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("AVAILABLE ACTIONS"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptIsJSONFormatted) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    // Prompt should contain JSON format instructions
    EXPECT_NE(prompt.find("JSON"), std::string::npos);
    EXPECT_NE(prompt.find("{"), std::string::npos);
    EXPECT_NE(prompt.find("\"action\""), std::string::npos);
}

TEST_F(PromptBuilderTest, MultipleMemoryEvents) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    AIMemory memory;
    for (int i = 0; i < 10; ++i) {
        memory.add_event("event_type", "Event " + std::to_string(i));
    }
    reg.emplace<AIMemory>(entity, memory);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = &memory;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("YOUR MEMORY"), std::string::npos);
}

TEST_F(PromptBuilderTest, UnreadMessages) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    AIMemory memory;
    memory.add_message("Hello from Bob");
    memory.add_message("See you later");
    reg.emplace<AIMemory>(entity, memory);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = &memory;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("UNREAD MESSAGES"), std::string::npos);
    EXPECT_NE(prompt.find("Hello from Bob"), std::string::npos);
}

TEST_F(PromptBuilderTest, AvailableActionsPrompt) {
    std::string actions_prompt = builder.build_available_actions_prompt();

    EXPECT_FALSE(actions_prompt.empty());
    // Should list action types
    EXPECT_TRUE(actions_prompt.find("Wander") != std::string::npos ||
                actions_prompt.find("Eat") != std::string::npos ||
                actions_prompt.find("Harvest") != std::string::npos);
}

TEST_F(PromptBuilderTest, EmptyNearbyEntities) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;
    ctx.nearby_entities = "";  // Empty

    std::string prompt = builder.build_decision_prompt(ctx);

    // Should not have nearby entities section if empty
    // (depends on implementation)
    EXPECT_FALSE(prompt.empty());
}

TEST_F(PromptBuilderTest, MaxHungerAndEnergy) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 10.0f;
    ctx.energy = 10.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("10"), std::string::npos);
}

TEST_F(PromptBuilderTest, ZeroHungerAndEnergy) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality = nullptr;
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;
    ctx.energy = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_FALSE(prompt.empty());
}
