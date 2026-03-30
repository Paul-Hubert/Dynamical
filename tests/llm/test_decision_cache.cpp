#include <gtest/gtest.h>
#include <llm/decision_cache.h>
#include <fixtures/test_fixtures.h>

class DecisionCacheTest : public Phase3TestFixture {
protected:
    DecisionCache cache;
};

TEST_F(DecisionCacheTest, GenerateKeyBasic) {
    std::string key = cache.generate_key(12345, 5.0f, 7.0f, "nearby_summary");
    EXPECT_FALSE(key.empty());
}

TEST_F(DecisionCacheTest, GenerateKeySameSeed) {
    std::string key1 = cache.generate_key(12345, 5.0f, 7.0f, "summary");
    std::string key2 = cache.generate_key(12345, 5.0f, 7.0f, "summary");
    EXPECT_EQ(key1, key2);
}

TEST_F(DecisionCacheTest, GenerateKeyDifferentSeed) {
    std::string key1 = cache.generate_key(12345, 5.0f, 7.0f, "summary");
    std::string key2 = cache.generate_key(54321, 5.0f, 7.0f, "summary");
    EXPECT_NE(key1, key2);
}

TEST_F(DecisionCacheTest, GenerateKeyDifferentHunger) {
    std::string key1 = cache.generate_key(12345, 2.0f, 7.0f, "summary");  // Low hunger
    std::string key2 = cache.generate_key(12345, 5.0f, 7.0f, "summary");  // Medium hunger
    std::string key3 = cache.generate_key(12345, 8.0f, 7.0f, "summary");  // High hunger
    EXPECT_NE(key1, key2);
    EXPECT_NE(key2, key3);
}

TEST_F(DecisionCacheTest, GenerateKeyDifferentEnergy) {
    std::string key1 = cache.generate_key(12345, 5.0f, 2.0f, "summary");  // Low energy
    std::string key2 = cache.generate_key(12345, 5.0f, 5.0f, "summary");  // Medium energy
    std::string key3 = cache.generate_key(12345, 5.0f, 8.0f, "summary");  // High energy
    EXPECT_NE(key1, key2);
    EXPECT_NE(key2, key3);
}

TEST_F(DecisionCacheTest, GenerateKeyDifferentSummary) {
    std::string key1 = cache.generate_key(12345, 5.0f, 7.0f, "summary1");
    std::string key2 = cache.generate_key(12345, 5.0f, 7.0f, "summary2");
    EXPECT_NE(key1, key2);
}

TEST_F(DecisionCacheTest, PutAndGet) {
    std::string key = "test_key";
    json decision = {
        {"action", "Eat"},
        {"resource", "berry"}
    };

    cache.put(key, decision);

    json retrieved;
    bool found = cache.get(key, retrieved);
    EXPECT_TRUE(found);
    EXPECT_EQ(retrieved["action"], "Eat");
    EXPECT_EQ(retrieved["resource"], "berry");
}

TEST_F(DecisionCacheTest, GetNonexistent) {
    json retrieved;
    bool found = cache.get("nonexistent_key", retrieved);
    EXPECT_FALSE(found);
}

TEST_F(DecisionCacheTest, CacheHitDifferentSummaries) {
    // Different summaries should map to different keys
    std::string key1 = cache.generate_key(12345, 5.0f, 7.0f, "summary1");
    std::string key2 = cache.generate_key(12345, 5.0f, 7.0f, "summary2");

    json decision1 = {{"action", "Eat"}};
    json decision2 = {{"action", "Wander"}};

    cache.put(key1, decision1);
    cache.put(key2, decision2);

    json retrieved1, retrieved2;
    EXPECT_TRUE(cache.get(key1, retrieved1));
    EXPECT_TRUE(cache.get(key2, retrieved2));
    EXPECT_EQ(retrieved1["action"], "Eat");
    EXPECT_EQ(retrieved2["action"], "Wander");
}

TEST_F(DecisionCacheTest, Discretization) {
    // Test hunger discretization (0-10 into 3 buckets)
    // All values in same bucket should generate same key part
    std::string key1 = cache.generate_key(12345, 1.0f, 5.0f, "summary");   // 0-3 -> 0
    std::string key2 = cache.generate_key(12345, 2.5f, 5.0f, "summary");  // 0-3 -> 0
    std::string key3 = cache.generate_key(12345, 3.4f, 5.0f, "summary");  // 0-3 -> 0
    std::string key4 = cache.generate_key(12345, 4.0f, 5.0f, "summary");  // 4-6 -> 1

    // Keys 1-3 should be the same, key4 should be different
    EXPECT_EQ(key1, key2);
    EXPECT_EQ(key2, key3);
    EXPECT_NE(key3, key4);

    // Test energy discretization
    std::string key5 = cache.generate_key(12345, 5.0f, 1.0f, "summary");   // 0-3 -> 0
    std::string key6 = cache.generate_key(12345, 5.0f, 4.0f, "summary");   // 4-6 -> 1
    EXPECT_NE(key5, key6);
}

TEST_F(DecisionCacheTest, ClearCache) {
    cache.put("key1", json{{"action", "Eat"}});
    cache.put("key2", json{{"action", "Wander"}});

    EXPECT_EQ(cache.size(), 2);

    cache.clear();
    EXPECT_EQ(cache.size(), 0);

    json retrieved;
    EXPECT_FALSE(cache.get("key1", retrieved));
    EXPECT_FALSE(cache.get("key2", retrieved));
}

TEST_F(DecisionCacheTest, MultipleDecisions) {
    // Store multiple decisions
    for (int i = 0; i < 10; ++i) {
        std::string key = cache.generate_key(12345 + i, 5.0f, 7.0f, "summary");
        json decision = {
            {"action", "Action" + std::to_string(i)},
            {"index", i}
        };
        cache.put(key, decision);
    }

    EXPECT_EQ(cache.size(), 10);

    // Verify retrieval
    for (int i = 0; i < 10; ++i) {
        std::string key = cache.generate_key(12345 + i, 5.0f, 7.0f, "summary");
        json retrieved;
        EXPECT_TRUE(cache.get(key, retrieved));
        EXPECT_EQ(retrieved["index"], i);
    }
}

TEST_F(DecisionCacheTest, OverwriteExisting) {
    std::string key = "test_key";

    json decision1 = {{"action", "Eat"}, {"value", 1}};
    cache.put(key, decision1);

    json retrieved1;
    EXPECT_TRUE(cache.get(key, retrieved1));
    EXPECT_EQ(retrieved1["value"], 1);

    // Overwrite with new decision
    json decision2 = {{"action", "Wander"}, {"value", 2}};
    cache.put(key, decision2);

    json retrieved2;
    EXPECT_TRUE(cache.get(key, retrieved2));
    EXPECT_EQ(retrieved2["value"], 2);
    EXPECT_EQ(cache.size(), 1);
}

TEST_F(DecisionCacheTest, HungerBoundaries) {
    // Test boundaries between hunger discretization buckets
    std::string key_3 = cache.generate_key(12345, 3.0f, 5.0f, "summary");
    std::string key_3_5 = cache.generate_key(12345, 3.5f, 5.0f, "summary");
    std::string key_3_9 = cache.generate_key(12345, 3.9f, 5.0f, "summary");
    std::string key_4 = cache.generate_key(12345, 4.0f, 5.0f, "summary");

    EXPECT_EQ(key_3, key_3_5);        // Both in low bucket
    EXPECT_EQ(key_3_5, key_3_9);      // Both in low bucket
    EXPECT_NE(key_3_9, key_4);        // Different buckets
}

TEST_F(DecisionCacheTest, EnergyBoundaries) {
    // Test boundaries between energy discretization buckets
    std::string key_3 = cache.generate_key(12345, 5.0f, 3.0f, "summary");
    std::string key_3_5 = cache.generate_key(12345, 5.0f, 3.5f, "summary");
    std::string key_3_9 = cache.generate_key(12345, 5.0f, 3.9f, "summary");
    std::string key_4 = cache.generate_key(12345, 5.0f, 4.0f, "summary");

    EXPECT_EQ(key_3, key_3_5);        // Both in low bucket
    EXPECT_EQ(key_3_5, key_3_9);      // Both in low bucket
    EXPECT_NE(key_3_9, key_4);        // Different buckets
}
