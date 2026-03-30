#include <ai/action_registry.h>
#include <ai/action_descriptor.h>

// Stub for tests: registers minimal descriptors without actual action implementations
namespace dy {
void ActionRegistry::initialize_descriptors() {
    register_descriptor({ActionID::Wander, "Wander", {}, nullptr});
    register_descriptor({ActionID::Eat,    "Eat",    {}, nullptr});
    register_descriptor({ActionID::Harvest,"Harvest",{}, nullptr});
}
} // namespace dy
