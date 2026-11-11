#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "logic/animals/animal_agent.h"
#include "scene/scenario_runner.h"
#include "util/paths.h"

using namespace dy;

// Not a test: a trace of a body standing on its drives, for whoever tunes the generator.
//   set AA_DEBUG=deer_red && dynamical_tests -tc="AA.trace*"
TEST_CASE("AA.trace of a standing body (set AA_DEBUG=<species>)") {
    const char* species = std::getenv("AA_DEBUG");
    if(!species) return;
    const char* ticks_text = std::getenv("AA_DEBUG_TICKS");
    const int ticks = ticks_text ? std::atoi(ticks_text) : 240;
    ScenarioRunner runner("animal_stand", { { "body", libraryResourceFile(std::string("animals/bodies/") + species + ".body.json").string() } }, SceneLoadOptions {}, 1);
    auto& world = runner.world(0);
    auto& bot = world.reg.ctx().get<AnimalBot>();
    bot.immortal = true;
    const AnimalBody& body = *bot.body;
    for(int t = 0; t < ticks; t++) {
        world.tick();
        if(t % 60 == 0) {
            for(size_t j = 0; j < body.def.joints.size(); j++) {
                const JointDef& joint = body.def.joints[j];
                if(!body.drives[j].support) continue;
                JointAngles a = jointAngles(body, (int) j, bot.rotation[joint.parent], bot.rotation[joint.child]);
                glm::vec3 st = body.drives[j].stand;
                glm::vec3 up = bot.rotation[joint.child] * glm::vec3(0, 1, 0);
                std::printf("   %-24s twist %+.3f (stand %+.3f, lim %+.2f %+.2f) swing %+.3f %+.3f (stand %+.3f %+.3f lim %.2f %.2f) child up %.3f %.3f %.3f y %.3f\n", joint.name.c_str(), a.twist, st.x,
                    joint.twist_min, joint.twist_max, 2 * std::asin(a.swing_y), 2 * std::asin(a.swing_z), st.y, st.z, joint.swing_y, joint.swing_z, up.x, up.y, up.z, bot.position[joint.child].y);
            }
        }
        if(t % 20 == 0 || t < 5) {
            glm::dvec3 com(0.);
            for(size_t s = 0; s < bot.position.size(); s++) {
                const SegmentDef& seg = body.def.segments[s];
                com += glm::dvec3(bot.position[s] + bot.rotation[s] * seg.center_of_mass) * (double) seg.percentage / 100.;
                if(!body.may_touch[s] || seg.shapes[0].type != ShapeDef::Type::Box) continue;
                const ShapeDef& shape = seg.shapes[0];
                float lo = 1e9f, hi = -1e9f, back = -1e9f, front = 1e9f;
                for(int c = 0; c < 4; c++) {
                    glm::vec3 corner = bot.position[s] + bot.rotation[s] * (shape.center + shape.rotation * (shape.half_extents * glm::vec3(c & 1 ? 1.f : -1.f, -1.f, c & 2 ? 1.f : -1.f)));
                    lo = std::min(lo, corner.y); hi = std::max(hi, corner.y); back = std::max(back, corner.z); front = std::min(front, corner.z);
                }
                std::printf("      sole %-22s y %+.4f to %+.4f, z %+.3f to %+.3f\n", seg.name.c_str(), lo, hi, front, back);
            }
            std::printf("      com %.3f %.3f %.3f\n", com.x, com.y, com.z);
            glm::vec3 up = bot.rotation[0] * glm::vec3(0, 1, 0);
            int segment = -1;
            int fallen = bot.fallen(&segment);
            std::printf("t %d trunk y %.3f pos %.2f %.2f up %.2f %.2f %.2f fallen %d %s clamps %llu\n", t, bot.position[0].y, bot.position[0].x, bot.position[0].z, up.x, up.y, up.z, fallen,
                segment >= 0 ? body.def.segments[segment].name.c_str() : "-", (unsigned long long) bot.target_clamps);
        }
    }
}
