#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "scene/system_registry.h"
#include "physics/jolt_runtime.h"
#ifdef DYNAMICAL_ONNX
#include "onnx/policy_systems.h"
#endif

int main(int argc, char** argv) {

    dy::JoltRuntime jolt;
    dy::registerSimSystems();
#ifdef DYNAMICAL_ONNX
    dy::registerPolicySystems();
#endif

    doctest::Context context(argc, argv);
    return context.run();

}
