#ifndef ITEM_H
#define ITEM_H

#include <array>
#include <string_view>
#include <cstddef>

// Single source of truth. To add a new item: one X(id, "string") line here.
#define DY_ITEMS(X)          \
    X(null,          "null")          \
    X(berry,         "berry")         \
    X(wood,          "wood")          \
    X(stone,         "stone")         \
    X(ore,           "ore")           \
    X(fish,          "fish")          \
    X(meat,          "meat")          \
    X(plank,         "plank")         \
    X(wooden_sword,  "wooden_sword")  \
    X(stone_pickaxe, "stone_pickaxe") \
    X(cooked_meat,   "cooked_meat")   \
    X(cooked_fish,   "cooked_fish")

namespace dy {

namespace detail {

constexpr std::size_t fnv1a(std::string_view s) noexcept {
    std::size_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

constexpr bool is_prime(std::size_t n) noexcept {
    if (n < 2) return false;
    for (std::size_t i = 2; i * i <= n; ++i)
        if (n % i == 0) return false;
    return true;
}

constexpr std::size_t next_prime(std::size_t n) noexcept {
    while (!is_prime(n)) ++n;
    return n;
}

} // namespace detail

class Item {
public:
    enum ID : int {
#define X(id, str) id,
        DY_ITEMS(X)
#undef X
        COUNT
    };

    // String table — indexed by enum ordinal, O(1) to_string
    static constexpr std::array<std::string_view, static_cast<std::size_t>(ID::COUNT)> k_names = {
#define X(id, str) str,
        DY_ITEMS(X)
#undef X
    };

    // Hash table — optimal prime size, built at compile time
    static constexpr std::size_t k_table_size = detail::next_prime(static_cast<std::size_t>(ID::COUNT) * 2);

    struct HashEntry { std::string_view key{}; ID value{ID::null}; };

    static constexpr std::array<HashEntry, k_table_size> k_table = []() constexpr {
        std::array<HashEntry, k_table_size> t{};
        for (std::size_t i = 0; i < static_cast<std::size_t>(ID::COUNT); ++i) {
            std::size_t slot = detail::fnv1a(k_names[i]) % k_table_size;
            while (t[slot].key.data() != nullptr)
                slot = (slot + 1) % k_table_size;
            t[slot] = {k_names[i], static_cast<ID>(i)};
        }
        return t;
    }();

    [[nodiscard]] static constexpr std::string_view to_string(ID id) noexcept {
        return k_names[static_cast<std::size_t>(id)];
    }

    [[nodiscard]] static constexpr ID from_string(std::string_view s) noexcept {
        std::size_t slot = detail::fnv1a(s) % k_table_size;
        while (k_table[slot].key.data() != nullptr) {
            if (k_table[slot].key == s) return k_table[slot].value;
            slot = (slot + 1) % k_table_size;
        }
        return ID::null;
    }

    Item(ID id, int amount) : id(id), amount(amount) {}
    ID id;
    int amount = 0;
};

} // namespace dy

#endif
