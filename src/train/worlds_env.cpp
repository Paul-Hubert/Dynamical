#include "worlds_env.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <map>
#include <set>

#include "mlagents/side_channels.h"
#include "scene/world.h"
#include "util/profile_workers.h"

using namespace dy;
namespace co = mlagents::co;

namespace {

// "6 + 5x6 observations, 2 + [2] actions": what the agents of a behaviour must share
std::string layoutOf(const AgentIO& io) {
    std::string s = std::to_string(io.observation_size);
    for(auto& sensor : io.sensors) {
        s += " + ";
        if(sensor.rows > 0) s += std::to_string(sensor.rows) + "x";
        s += std::to_string(sensor.size);
    }
    s += " observations, " + std::to_string(io.action_size);
    if(!io.branches.empty()) {
        s += " + [";
        for(size_t b = 0; b < io.branches.size(); b++) s += (b ? ", " : "") + std::to_string(io.branches[b]);
        s += "]";
    }
    return s + " actions";
}

bool isAgent(const AgentIO* io) {
    return io && !io->behavior.empty();
}

void writeObservation(co::AgentInfoProto& info, const std::string& name, const std::vector<float>& data, int rows, int size) {
    auto* obs = info.add_observations();
    obs->set_compression_type(co::NONE);
    obs->mutable_float_data()->mutable_data()->Add(data.begin(), data.end());
    if(rows > 0) {
        // A buffer sensor: variable size along the rows, none along the values
        obs->add_shape(rows);
        obs->add_shape(size);
        obs->add_dimension_properties(4);
        obs->add_dimension_properties(1);
    } else {
        obs->add_shape(size);
        obs->add_dimension_properties(0);
    }
    obs->set_observation_type(co::DEFAULT);
    obs->set_name(name);
}

}

std::string WorldsEnvironment::check(ScenarioRunner& runner) {
    if(runner.worldCount() == 0) return "the scene has no worlds";
    uint32_t first_period = 0;
    std::map<std::string, std::string> layouts;
    auto layout = [&](const AgentIO& io, size_t world) -> std::string {
        std::string l = layoutOf(io);
        auto [it, fresh] = layouts.emplace(io.behavior, l);
        if(!fresh && it->second != l) return "world " + std::to_string(world) + " has an agent of " + io.behavior + " with " + l + ", others have " + it->second;
        return {};
    };
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        const auto* io = reg.ctx().find<AgentIO>();
        bool any = isAgent(io) || reg.ctx().find<AgentsReset>();
        if(isAgent(io)) {
            if(auto error = layout(*io, i); !error.empty()) return error;
        }
        for(auto e : reg.view<AgentIO>()) {
            const auto& agent = reg.get<AgentIO>(e);
            if(agent.behavior.empty()) return "world " + std::to_string(i) + " has an agent without a behaviour";
            if(auto error = layout(agent, i); !error.empty()) return error;
            any = true;
        }
        if(!any) return "world " + std::to_string(i) + " has no agent";
        uint32_t period = reg.ctx().get<WorldClock>().decision_period;
        if(i == 0) first_period = period;
        else if(period != first_period) return "world " + std::to_string(i) + " has another decision period than world 0";
    }
    return {};
}

std::vector<std::string> WorldsEnvironment::behaviors(ScenarioRunner& runner) {
    std::set<std::string> names;
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        if(const auto* io = reg.ctx().find<AgentIO>(); isAgent(io)) names.insert(io->behavior);
        for(auto e : reg.view<AgentIO>()) names.insert(reg.get<AgentIO>(e).behavior);
    }
    return { names.begin(), names.end() };
}

WorldsEnvironment::WorldsEnvironment(ScenarioRunner& runner, unsigned threads) : runner(runner) {
    if(threads > 1) executor = std::make_unique<tf::Executor>(threads, profile::namedWorkers());
    period = runner.world(0).reg.ctx().get<WorldClock>().decision_period;
    // The agent of a world has its world's index; components come after
    next_id = (int32_t) runner.worldCount();
}

WorldsEnvironment::~WorldsEnvironment() = default;

AgentIO* WorldsEnvironment::find(const Slot& slot) {
    auto& reg = runner.world(slot.world).reg;
    if(slot.entity == entt::null) return reg.ctx().find<AgentIO>();
    return reg.valid(slot.entity) ? reg.try_get<AgentIO>(slot.entity) : nullptr;
}

bool WorldsEnvironment::gather() {

    DY_ZONE_N("Gather agents");

    auto add = [&](AgentIO& io, Slot slot) {
        auto& group = groups[io.behavior];
        std::string layout = layoutOf(io);
        if(group.layout.empty()) {
            group.layout = layout;
            group.observation_size = io.observation_size;
            group.action_size = io.action_size;
            group.branches = io.branches;
            for(auto& sensor : io.sensors) group.sensors.emplace_back(sensor.rows, sensor.size);
        } else if(group.layout != layout) {
            problem = "an agent of " + io.behavior + " in world " + std::to_string(slot.world) + " has " + layout + ", the behaviour has " + group.layout;
            return false;
        }
        group.pending.push_back(slot);
        return true;
    };

    std::vector<entt::entity> list;
    for(size_t w = 0; w < runner.worldCount(); w++) {
        auto& reg = runner.world(w).reg;
        if(auto* io = reg.ctx().find<AgentIO>(); isAgent(io)) {
            io->id = (int) w;
            if(!add(*io, Slot { (uint32_t) w, entt::null })) return false;
        }
        // In entity order: a view's own order changes when components are removed
        list.clear();
        for(auto e : reg.view<AgentIO>()) {
            const auto& io = reg.get<AgentIO>(e);
            if(io.requested || io.done) list.push_back(e);
        }
        std::sort(list.begin(), list.end());
        for(auto e : list) {
            auto& io = reg.get<AgentIO>(e);
            if(io.behavior.empty()) {
                problem = "an agent in world " + std::to_string(w) + " has no behaviour";
                return false;
            }
            if(io.id < 0) io.id = next_id++;
            io.requested = false;
            if(!add(io, Slot { (uint32_t) w, e })) return false;
        }
    }
    return true;

}

size_t WorldsEnvironment::pendingCount() const {
    size_t n = 0;
    for(auto& [name, group] : groups) n += group.pending.size();
    return n;
}

std::string WorldsEnvironment::describe() const {
    std::map<std::string, std::pair<size_t, std::string>> found;
    auto count = [&](const AgentIO& io) {
        auto& entry = found[io.behavior];
        entry.first++;
        entry.second = layoutOf(io);
    };
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        if(const auto* io = reg.ctx().find<AgentIO>(); isAgent(io)) count(*io);
        for(auto e : reg.view<AgentIO>()) count(reg.get<AgentIO>(e));
    }
    std::string s;
    for(auto& [name, entry] : found) s += (s.empty() ? "" : ", ") + std::to_string(entry.first) + " " + name + " (" + entry.second + ")";
    return s.empty() ? "no agent yet" : s;
}

void WorldsEnvironment::reset() {
    // A fresh observation of every agent without stepping
    for(auto& [name, group] : groups) {
        group.pending.clear();
        group.sent.clear();
        group.done.clear();
    }
    for(size_t i = 0; i < runner.worldCount(); i++) {
        auto& reg = runner.world(i).reg;
        if(auto* io = reg.ctx().find<AgentIO>()) io->reset_requested = true;
        if(auto* all = reg.ctx().find<AgentsReset>()) all->requested = true;
        runner.world(i).systems.fixed_observe_only();
    }
    gather();
}

bool WorldsEnvironment::step(const co::UnityRLInputProto& input, std::string& error) {

    DY_ZONE_N("Environment step");

    if(!problem.empty()) {
        error = problem;
        return false;
    }

    for(auto& [name, group] : groups) {

        size_t requested = 0;
        for(char d : group.done) requested += d ? 0 : 1;

        const co::UnityRLInputProto::ListAgentActionProto* list = nullptr;
        if(auto it = input.agent_actions().find(name); it != input.agent_actions().end()) list = &it->second;
        size_t received = list ? (size_t) list->value_size() : 0;
        if(received != requested) {
            error = "expected actions for " + std::to_string(requested) + " agents of " + name + ", got " + std::to_string(received);
            return false;
        }

        int k = 0;
        for(size_t i = 0; i < group.sent.size(); i++) {
            AgentIO* io = find(group.sent[i]);
            if(!io) {
                error = "an agent of " + name + " left world " + std::to_string(group.sent[i].world) + " without a terminal step";
                return false;
            }
            if(group.done[i]) {
                std::fill(io->action.begin(), io->action.end(), 0.f);
                std::fill(io->discrete.begin(), io->discrete.end(), 0);
                continue;
            }
            const auto& action = list->value(k++);
            if(action.continuous_actions_size() != io->action_size) {
                error = "expected " + std::to_string(io->action_size) + " continuous actions for " + name + ", got " + std::to_string(action.continuous_actions_size());
                return false;
            }
            if(action.discrete_actions_size() != (int) io->branches.size()) {
                error = "expected " + std::to_string(io->branches.size()) + " discrete actions for " + name + ", got " + std::to_string(action.discrete_actions_size());
                return false;
            }
            for(int a = 0; a < io->action_size; a++) io->action[a] = action.continuous_actions(a);
            for(size_t b = 0; b < io->branches.size(); b++) {
                int choice = action.discrete_actions((int) b);
                if(choice < 0 || choice >= io->branches[b]) {
                    error = "discrete action " + std::to_string(choice) + " of " + name + " is outside its branch of " + std::to_string(io->branches[b]);
                    return false;
                }
                io->discrete[b] = choice;
            }
        }

    }

    // As Unity, which only talks to the trainer when an agent asks for a decision or ends
    const int max_windows = 100000;
    for(int n = 0; ; n++) {
        runner.step(period, executor.get());
        if(!gather()) {
            error = problem;
            return false;
        }
        if(pendingCount() > 0) break;
        if(n >= max_windows) {
            error = "no agent asked for a decision in " + std::to_string(max_windows) + " windows";
            return false;
        }
    }
    return true;

}

void WorldsEnvironment::write(co::UnityOutputProto& output) {

    DY_ZONE_N("Write observations");

    auto* rl = output.mutable_rl_output();
    struct Sum { double total = 0.; int count = 0; int aggregation = 0; };
    std::map<std::string, Sum> stats;

    for(auto& [name, group] : groups) {

        group.sent.clear();
        group.done.clear();
        if(group.pending.empty()) continue;

        auto& infos = (*rl->mutable_agentinfos())[name];
        for(const Slot& slot : group.pending) {
            AgentIO* found = find(slot);
            if(!found) continue;
            AgentIO& io = *found;
            auto* info = infos.add_value();
            info->set_id(io.id);
            info->set_reward(io.reward);
            info->set_done(io.done);
            info->set_max_step_reached(io.done && io.truncated);
            info->set_group_id(0);
            info->set_group_reward(0.f);
            writeObservation(*info, io.sensor_name, io.observation, 0, io.observation_size);
            for(auto& sensor : io.sensors) writeObservation(*info, sensor.name, sensor.data, sensor.rows, sensor.size);
            group.sent.push_back(slot);
            group.done.push_back(io.done ? 1 : 0);
            for(auto& s : io.stats) {
                auto& sum = stats[s.key];
                sum.total += s.value;
                sum.count++;
                sum.aggregation = s.aggregation;
            }
            // A component's stats are its own to give once; the agent of a world rewrites its every window
            if(slot.entity != entt::null) io.stats.clear();
        }
        group.pending.clear();

        // With the first output that has an agent of the behaviour: the trainer reads the observation layout there
        if(!group.brain_sent && !group.sent.empty()) {
            auto* brain = output.mutable_rl_initialization_output()->add_brain_parameters();
            brain->set_brain_name(name);
            brain->set_is_training(true);
            brain->mutable_action_spec()->set_num_continuous_actions(group.action_size);
            brain->mutable_action_spec()->set_num_discrete_actions((int) group.branches.size());
            for(int size : group.branches) brain->mutable_action_spec()->add_discrete_branch_sizes(size);
            group.brain_sent = true;
        }

    }

    // One averaged value per key and window, with its Unity aggregation (stats_side_channel.py averages them over
    // the summary period). Per-agent values would be one message per key, agent and decision
    if(!stats.empty()) {
        std::vector<mlagents::ChannelMessage> messages;
        for(auto& [key, sum] : stats) {
            messages.push_back({ mlagents::statsChannel(), mlagents::writeStat(key, (float) (sum.total / sum.count), (mlagents::StatAggregation) sum.aggregation) });
        }
        rl->set_side_channel(mlagents::writeChannelMessages(messages));
    }

}
