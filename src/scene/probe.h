#ifndef DY_PROBE_H
#define DY_PROBE_H

#include <entt/entt.hpp>

#include <cstdint>
#include <fstream>
#include <string>

namespace dy {

struct RunArgs;

// Behaviour probe (probe=<file.csv> probe_entities=RigidBody probe_every=60): every probe_every frames, one row
// per entity holding the component, ordered by entity id. index is the rank in that order.
// Columns: frame, index, entity, x, y, z, qw, qx, qy, qz
class Probe {
public:
    explicit Probe(const RunArgs& args);

    bool failed() const { return !error.empty(); }
    void sample(const entt::registry& reg, uint64_t frame);

    std::string error;

private:
    std::ofstream file;
    std::string component;
    int every = 60;
};

}

#endif
