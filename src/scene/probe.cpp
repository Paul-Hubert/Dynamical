#include "probe.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "logic/components/rigid_body.h"
#include "logic/components/transform.h"
#include "scene/run_args.h"

using namespace dy;

Probe::Probe(const RunArgs& args) : file(args.probe, std::ios::binary), component(args.probe_entities), every(args.probe_every) {

    if(component != "RigidBody" && component != "Transform") {
        error = "probe_entities=" + component + ": only RigidBody and Transform can be probed";
        return;
    }
    if(!file) {
        error = "cannot write probe file " + args.probe;
        return;
    }
    file << "frame,index,entity,x,y,z,qw,qx,qy,qz\n";

}

void Probe::sample(const entt::registry& reg, uint64_t frame) {

    if(failed() || frame % every != 0) return;

    std::vector<entt::entity> entities;
    if(component == "RigidBody") {
        for(auto entity : reg.view<Transform, RigidBody>()) entities.push_back(entity);
    } else {
        for(auto entity : reg.view<Transform>()) entities.push_back(entity);
    }
    std::sort(entities.begin(), entities.end());

    char line[512];
    for(size_t i = 0; i < entities.size(); i++) {
        auto& t = reg.get<Transform>(entities[i]);
        std::snprintf(line, sizeof(line), "%llu,%zu,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", (unsigned long long) frame, i,
            (unsigned) entt::to_integral(entities[i]), t.position.x, t.position.y, t.position.z,
            t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z);
        file << line;
    }
    file.flush();

}
