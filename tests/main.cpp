#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "scene/system_registry.h"

int main(int argc, char** argv) {

    dy::registerSimSystems();

    doctest::Context context(argc, argv);
    return context.run();

}
