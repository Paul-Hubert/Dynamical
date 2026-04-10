#include <ai/action_registry.h>
#include <ai/actions/wander_action.h>
#include <ai/actions/eat_action.h>
#include <ai/actions/harvest_action.h>
#include <logic/components/storage.h>
#include <logic/components/item.h>
#include <logic/components/building.h>
#include <ai/actions/mine_action.h>
#include <ai/actions/build_action.h>
#include <ai/actions/craft_action.h>
#include <ai/actions/trade_action.h>
#include <ai/actions/talk_action.h>
#include <ai/actions/sleep_action.h>
#include <ai/actions/explore_action.h>
#include <ai/actions/flee_action.h>
#include <ai/actions/chop_action.h>
#include <ai/actions/hunt_action.h>
#include <ai/actions/fish_action.h>
#include <ai/crafting/recipe_registry.h>
#include "util/log.h"

namespace dy {

void ActionRegistry::initialize_descriptors() {
    // Wander - no prerequisites
    {
        ActionDescriptor desc{
            .id = ActionID::Wander,
            .name = "Wander",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<WanderAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Eat - prerequisite: needs food source (Harvest)
    {
        ActionDescriptor desc{
            .id = ActionID::Eat,
            .name = "Eat",
            .prerequisites = {
                {
                    .description = "needs food source",
                    .is_satisfied = [](auto& r, auto e, const auto&) {
                        if (!r.template all_of<Storage>(e)) return false;
                        return r.template get<Storage>(e).amount(Item::berry) > 0;
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Harvest; }
                }
            },
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<EatAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Harvest - no prerequisites
    {
        ActionDescriptor desc{
            .id = ActionID::Harvest,
            .name = "Harvest",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<HarvestAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Mine - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Mine,
            .name = "Mine",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<MineAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Chop - no prerequisites
    {
        ActionDescriptor desc{
            .id = ActionID::Chop,
            .name = "Chop",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<ChopAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Hunt - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Hunt,
            .name = "Hunt",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<HuntAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Build - needs wood and stone (checked independently so both can be queued)
    {
        ActionDescriptor desc{
            .id = ActionID::Build,
            .name = "Build",
            .prerequisites = {
                {
                    .description = "needs wood",
                    .is_satisfied = [](auto& r, auto e, const auto&) {
                        if (!r.template all_of<Storage>(e)) return false;
                        auto& s = r.template get<Storage>(e);
                        auto& smallest = get_building_templates()[Building::small_building];
                        return s.amount(Item::wood) >= smallest.wood_cost();
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Chop; }
                },
                {
                    .description = "needs stone",
                    .is_satisfied = [](auto& r, auto e, const auto&) {
                        if (!r.template all_of<Storage>(e)) return false;
                        auto& s = r.template get<Storage>(e);
                        auto& smallest = get_building_templates()[Building::small_building];
                        return s.amount(Item::stone) >= smallest.stone_cost();
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Mine; }
                }
            },
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<BuildAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Sleep - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Sleep,
            .name = "Sleep",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<SleepAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Trade - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Trade,
            .name = "Trade",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<TradeAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Talk - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Talk,
            .name = "Talk",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<TalkAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Craft - prerequisites check recipe ingredients
    {
        ActionDescriptor desc{
            .id = ActionID::Craft,
            .name = "Craft",
            .prerequisites = {
                {
                    .description = "needs wood",
                    .is_satisfied = [](auto& r, auto e, const auto& p) {
                        const auto* recipe = RecipeRegistry::instance().find(p.recipe);
                        if (!recipe) return true; // unknown recipe — let action stage fail with message
                        if (!r.template all_of<Storage>(e)) return false;
                        auto& s = r.template get<Storage>(e);
                        for (auto& [item, amt] : recipe->inputs)
                            if (item == Item::wood && s.amount(Item::wood) < amt) return false;
                        return true;
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Chop; }
                },
                {
                    .description = "needs stone",
                    .is_satisfied = [](auto& r, auto e, const auto& p) {
                        const auto* recipe = RecipeRegistry::instance().find(p.recipe);
                        if (!recipe) return true;
                        if (!r.template all_of<Storage>(e)) return false;
                        auto& s = r.template get<Storage>(e);
                        for (auto& [item, amt] : recipe->inputs)
                            if (item == Item::stone && s.amount(Item::stone) < amt) return false;
                        return true;
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Mine; }
                },
                {
                    .description = "needs meat",
                    .is_satisfied = [](auto& r, auto e, const auto& p) {
                        const auto* recipe = RecipeRegistry::instance().find(p.recipe);
                        if (!recipe) return true;
                        if (!r.template all_of<Storage>(e)) return false;
                        auto& s = r.template get<Storage>(e);
                        for (auto& [item, amt] : recipe->inputs)
                            if (item == Item::meat && s.amount(Item::meat) < amt) return false;
                        return true;
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Hunt; }
                },
                {
                    .description = "needs fish",
                    .is_satisfied = [](auto& r, auto e, const auto& p) {
                        const auto* recipe = RecipeRegistry::instance().find(p.recipe);
                        if (!recipe) return true;
                        if (!r.template all_of<Storage>(e)) return false;
                        auto& s = r.template get<Storage>(e);
                        for (auto& [item, amt] : recipe->inputs)
                            if (item == Item::fish && s.amount(Item::fish) < amt) return false;
                        return true;
                    },
                    .resolve_action = [](auto&, auto, const auto&) { return ActionID::Fish; }
                }
            },
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<CraftAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Fish - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Fish,
            .name = "Fish",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<FishAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Explore - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Explore,
            .name = "Explore",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<ExploreAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }

    // Flee - no prerequisites (skeleton)
    {
        ActionDescriptor desc{
            .id = ActionID::Flee,
            .name = "Flee",
            .prerequisites = {},
            .create = [](entt::registry& r, entt::entity e, const ActionParams& p) {
                return std::make_unique<FleeAction>(r, e, p);
            }
        };
        register_descriptor(desc);
    }
}

} // namespace dy
