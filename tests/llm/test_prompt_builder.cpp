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

    PromptContext ctx = build_context(reg, entity);

    EXPECT_EQ(ctx.entity_name, "Alice");
    EXPECT_EQ(ctx.entity_id, entity);
    EXPECT_EQ(ctx.personality_type, "explorer");
    EXPECT_NE(ctx.memory, nullptr);
}

TEST_F(PromptBuilderTest, BuildContextWithoutIdentity) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    PromptContext ctx = build_context(reg, entity);

    // Falls back to Entity#id name when no EntityIdentity
    EXPECT_NE(ctx.entity_name, "");
    EXPECT_EQ(ctx.personality_type, "");
}

TEST_F(PromptBuilderTest, BuildContextWithoutMemory) {
    auto& reg = create_test_registry();
    entt::entity entity = reg.create();

    EntityIdentity identity;
    identity.name             = "Charlie";
    identity.personality_type = "craftsman";
    identity.personality_description = "Meticulous and proud of quality work.";
    identity.ready = true;
    reg.emplace<EntityIdentity>(entity, identity);

    PromptContext ctx = build_context(reg, entity);

    EXPECT_EQ(ctx.personality_type, "craftsman");
    EXPECT_EQ(ctx.memory, nullptr);
}

TEST_F(PromptBuilderTest, BuildSystemPrompt) {
    std::string system_prompt = builder.build_system_prompt("explorer",
        "Driven by curiosity and a love of discovery.");

    EXPECT_FALSE(system_prompt.empty());
    EXPECT_NE(system_prompt.find("explorer"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildSystemPromptEmpty) {
    std::string system_prompt = builder.build_system_prompt("", "");

    EXPECT_FALSE(system_prompt.empty());
    EXPECT_NE(system_prompt.find("decision"), std::string::npos);
}

TEST_F(PromptBuilderTest, BuildDecisionPrompt) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");
    AIMemory* memory = get_memory(reg, entity);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.personality_type = "explorer";
    ctx.personality_description = "Curious and adventurous.";
    ctx.memory = memory;
    ctx.hunger = 5.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_FALSE(prompt.empty());
    EXPECT_NE(prompt.find("Alice"), std::string::npos);
    EXPECT_NE(prompt.find("explorer"), std::string::npos);
    EXPECT_NE(prompt.find("Hunger"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsPersonalityDescription) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.personality_type = "hermit";
    ctx.personality_description = "A reclusive wanderer who prefers solitude.";
    ctx.memory = nullptr;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("hermit"), std::string::npos);
    EXPECT_NE(prompt.find("reclusive wanderer"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsMemoryEvents) {
    auto& reg = create_test_registry();
    entt::entity entity = create_test_entity("Alice");
    AIMemory* memory = get_memory(reg, entity);

    PromptContext ctx;
    ctx.entity_id = entity;
    ctx.entity_name = "Alice";
    ctx.memory = memory;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("spawned"), std::string::npos);
    EXPECT_NE(prompt.find("harvested"), std::string::npos);
    EXPECT_NE(prompt.find("talked"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsStateSection) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "TestEntity";
    ctx.memory = nullptr;
    ctx.hunger = 3.5f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("Hunger"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsNearbyPeople) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;
    ctx.nearby_human_names = "Bob, Charlie";

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("NEARBY PEOPLE"), std::string::npos);
    EXPECT_NE(prompt.find("Bob"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsNearbyResources) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;
    ctx.nearby_resources = R"([{"type": "berry_bush", "distance": 5}])";

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("NEARBY RESOURCES"), std::string::npos);
    EXPECT_NE(prompt.find("berry_bush"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptContainsAvailableActions) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("AVAILABLE ACTIONS"), std::string::npos);
}

TEST_F(PromptBuilderTest, PromptIsJSONFormatted) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("JSON"), std::string::npos);
    EXPECT_NE(prompt.find("{"), std::string::npos);
    EXPECT_NE(prompt.find("\"action\""), std::string::npos);
}

TEST_F(PromptBuilderTest, MultipleMemoryEvents) {
    AIMemory memory;
    for (int i = 0; i < 10; ++i) {
        memory.add_event("event_type", "Event " + std::to_string(i));
    }

    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = &memory;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("YOUR MEMORY"), std::string::npos);
}

TEST_F(PromptBuilderTest, UnreadMessages) {
    AIMemory memory;
    memory.add_message("Hello from Bob");
    memory.add_message("See you later");

    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = &memory;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("UNREAD MESSAGES"), std::string::npos);
    EXPECT_NE(prompt.find("Hello from Bob"), std::string::npos);
}

TEST_F(PromptBuilderTest, AvailableActionsPrompt) {
    std::string actions_prompt = builder.build_available_actions_prompt();

    EXPECT_FALSE(actions_prompt.empty());
    EXPECT_TRUE(actions_prompt.find("Wander") != std::string::npos ||
                actions_prompt.find("Eat") != std::string::npos ||
                actions_prompt.find("Harvest") != std::string::npos);
}

TEST_F(PromptBuilderTest, EmptyNearbyPeople) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;
    ctx.nearby_human_names = "";

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_FALSE(prompt.empty());
    EXPECT_EQ(prompt.find("NEARBY PEOPLE"), std::string::npos);
}

TEST_F(PromptBuilderTest, MaxHunger) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;
    ctx.hunger = 10.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("10"), std::string::npos);
}

TEST_F(PromptBuilderTest, ZeroHunger) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;
    ctx.hunger = 0.0f;

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_FALSE(prompt.empty());
}

TEST_F(PromptBuilderTest, ConversationPartnerNameInPrompt) {
    PromptContext ctx;
    ctx.entity_id = entt::null;
    ctx.entity_name = "Alice";
    ctx.memory = nullptr;
    ctx.conversation_partner = "Mira";
    ctx.conversation_history = "Mira: Hello!\n";

    std::string prompt = builder.build_decision_prompt(ctx);

    EXPECT_NE(prompt.find("Mira"), std::string::npos);
    EXPECT_NE(prompt.find("ACTIVE CONVERSATION"), std::string::npos);
}
