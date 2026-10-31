#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "scene/system_registry.h"
#include "physics/jolt_runtime.h"

int main(int argc, char** argv) {

    dy::JoltRuntime jolt;
    dy::registerSimSystems();

    doctest::Context context(argc, argv);
    return context.run();

}
