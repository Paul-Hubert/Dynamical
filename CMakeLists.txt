cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)

project(dynamical)

# Test names with spaces and punctuation (the test IDs of phases.md)
cmake_policy(SET CMP0110 NEW)

option(DYNAMICAL_BUILD_VIEWER "Build dynamical (Vulkan, SDL, OpenXR, shaders)" ON)   # OFF in CI
option(DYNAMICAL_TRAIN "Build the headless training executable" ON)
option(DYNAMICAL_TESTS "Build the tests" ON)
option(DYNAMICAL_BUILD_TOOLS "Build offline tools (grass raycast baker)" OFF)
option(DYNAMICAL_ONNX "Policy inference with the prebuilt ONNX Runtime (Windows x64)" ${WIN32})
set(DYNAMICAL_TRAIN_NAME "TrainingEnv" CACHE STRING "Output name of dynamical_train")

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "Build Noise Tool" FORCE)
add_subdirectory(lib/FastNoise2)

# Common flags for every target below (master set them on dynamical only)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if (MSVC)
    add_compile_options(/bigobj /utf-8)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Wno-sign-compare -Wno-unused-variable -Wno-unused-parameter -Wno-class-memaccess -Wno-unused-but-set-variable -Wno-reorder -Wno-missing-field-initializers -Wno-unused-function -Wno-type-limits)
endif()

find_package(EnTT CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_path(CEREAL_INCLUDE_DIRS "cereal")
find_path(STB_INCLUDE_DIRS "stb_image.h")

### SIMULATION LIBRARY: no Vulkan, SDL, OpenXR or ImGui (checked by the T1.1 header-boundary test)
file(GLOB_RECURSE SIM_SOURCES CONFIGURE_DEPENDS ./src/logic/*.cpp ./src/physics/*.cpp ./src/scene/*.cpp ./src/util/*.cpp)
add_library(dynamical_sim STATIC ${SIM_SOURCES})
target_include_directories(dynamical_sim PUBLIC src lib/FastNoise2/include ${CEREAL_INCLUDE_DIRS})
target_link_libraries(dynamical_sim PUBLIC EnTT::EnTT Jolt::Jolt glm::glm nlohmann_json::nlohmann_json Taskflow::Taskflow FastNoise)

### POLICY INFERENCE (dynamical_onnx: PolicyRunner, the Policy and ModelWatcher systems)
if (DYNAMICAL_ONNX)
    include(cmake/onnxruntime.cmake)
    file(GLOB ONNX_SOURCES CONFIGURE_DEPENDS ./src/onnx/*.cpp)
    add_library(dynamical_onnx STATIC ${ONNX_SOURCES})
    target_link_libraries(dynamical_onnx PUBLIC dynamical_sim onnxruntime::onnxruntime)
    target_compile_definitions(dynamical_onnx PUBLIC DYNAMICAL_ONNX)
    # onnxruntime.dll next to an executable (System32 has an older one of the same name)
    function(copy_onnxruntime target)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLLS} "$<TARGET_FILE_DIR:${target}>")
    endfunction()
endif()

### VIEWER
if (DYNAMICAL_BUILD_VIEWER)

add_subdirectory(lib/VulkanMemoryAllocator)

file(GLOB_RECURSE VIEW_SOURCES CONFIGURE_DEPENDS ./src/main.cpp ./src/view/*.cpp ./src/view/*.h ./src/renderer/*.cpp ./src/renderer/*.h ./src/extra/*.cpp ./src/extra/*.h)

add_executable(dynamical ${VIEW_SOURCES})

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(OpenXR CONFIG REQUIRED)

target_include_directories(dynamical PRIVATE lib lib/imgui lib/implot lib/VulkanMemoryAllocator/include ${SDL2_INCLUDE_DIR} ${STB_INCLUDE_DIRS})
target_link_libraries(dynamical PRIVATE dynamical_sim Vulkan::Vulkan ${SDL2_LIBRARIES} OpenXR::headers OpenXR::openxr_loader VulkanMemoryAllocator)
if (DYNAMICAL_ONNX)
    target_link_libraries(dynamical PRIVATE dynamical_onnx)
    copy_onnxruntime(dynamical)
endif()

target_sources(
  dynamical PRIVATE
  "lib/imgui/imgui.cpp"
  "lib/imgui/imgui_demo.cpp"
  "lib/imgui/imgui_draw.cpp"
  "lib/imgui/imgui_tables.cpp"
  "lib/imgui/imgui_widgets.cpp"
  "lib/implot/implot.cpp"
  "lib/implot/implot_demo.cpp"
  "lib/implot/implot_items.cpp"
)

# Copy resources to build
file(
    COPY resources/
    DESTINATION ${PROJECT_BINARY_DIR}/resources/
)

add_custom_command(TARGET dynamical POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PROJECT_BINARY_DIR}/resources"
)

### COMPILE SHADERS
set(GLSL_VALIDATOR "glslangValidator")

file(GLOB_RECURSE GLSL_SOURCE_FILES "src/renderer/shaders/*.glsl")

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_BINARY_DIR}/resources/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/resources/shaders/"
    # vulkan1.1 is needed for the subgroup operations in the marching cubes shader
    COMMAND ${GLSL_VALIDATOR} -V --target-env vulkan1.1 ${GLSL} -o ${SPIRV} -g
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

add_dependencies(dynamical shaders)

endif()

### HEADLESS TRAINING EXECUTABLE (TrainingEnv.exe: the trainer globs <path>.exe)
if (DYNAMICAL_TRAIN)
    # ML-Agents protocol: the fork's protos (src/mlagents/proto, VERSION), compiled with vcpkg's protoc and
    # grpc_cpp_plugin into the build directory
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    file(GLOB MLAGENTS_PROTOS ./src/mlagents/proto/mlagents_envs/communicator_objects/*.proto)
    file(GLOB MLAGENTS_SOURCES CONFIGURE_DEPENDS ./src/mlagents/*.cpp)
    add_library(dynamical_mlagents STATIC ${MLAGENTS_SOURCES} ${MLAGENTS_PROTOS})
    set(PROTO_OUT ${CMAKE_CURRENT_BINARY_DIR}/proto)
    file(MAKE_DIRECTORY ${PROTO_OUT})
    protobuf_generate(TARGET dynamical_mlagents LANGUAGE cpp IMPORT_DIRS ${CMAKE_SOURCE_DIR}/src/mlagents/proto PROTOC_OUT_DIR ${PROTO_OUT})
    protobuf_generate(TARGET dynamical_mlagents LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>" IMPORT_DIRS ${CMAKE_SOURCE_DIR}/src/mlagents/proto PROTOC_OUT_DIR ${PROTO_OUT})
    target_include_directories(dynamical_mlagents PUBLIC ${PROTO_OUT})
    target_link_libraries(dynamical_mlagents PUBLIC dynamical_sim gRPC::grpc++ protobuf::libprotobuf)

    # The engine's commit, stamped into envparams.txt
    set(VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/dynamical_version.h)
    execute_process(COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUT=${VERSION_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/git_version.cmake)
    add_custom_target(dynamical_version
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUT=${VERSION_HEADER} -P ${CMAKE_SOURCE_DIR}/cmake/git_version.cmake
        BYPRODUCTS ${VERSION_HEADER})

    # Train-side code in a library so the tests link it too
    file(GLOB_RECURSE TRAIN_SOURCES CONFIGURE_DEPENDS ./src/train/*.cpp)
    add_library(dynamical_train_core STATIC ${TRAIN_SOURCES})
    target_include_directories(dynamical_train_core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(dynamical_train_core PUBLIC dynamical_sim dynamical_mlagents)
    add_dependencies(dynamical_train_core dynamical_version)

    add_executable(dynamical_train src/train_main.cpp)
    target_link_libraries(dynamical_train PRIVATE dynamical_train_core)
    set_target_properties(dynamical_train PROPERTIES OUTPUT_NAME ${DYNAMICAL_TRAIN_NAME})

    # Deployed layout: the executable with resources/{scenes,prefabs,agents} next to it
    foreach(dir scenes prefabs agents)
        if (EXISTS ${CMAKE_SOURCE_DIR}/resources/${dir})
            add_custom_command(TARGET dynamical_train POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources/${dir}" "$<TARGET_FILE_DIR:dynamical_train>/resources/${dir}")
        endif()
    endforeach()
endif()

### IMAGE COMPARISON (visual tests)
add_library(imgdiff_core STATIC tools/imgdiff/imgdiff.cpp tools/imgdiff/stb_impl.cpp)
target_include_directories(imgdiff_core PUBLIC tools ${STB_INCLUDE_DIRS})
add_executable(imgdiff tools/imgdiff/main.cpp)
target_link_libraries(imgdiff PRIVATE imgdiff_core)

### TESTS
if (DYNAMICAL_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)
    include(${doctest_DIR}/doctest.cmake)

    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS ./tests/*.cpp)
    if (NOT DYNAMICAL_TRAIN)
        # The protocol tests and the MockTrainer need gRPC and the train code
        list(FILTER test_sources EXCLUDE REGEX "/tests/(protocol|support/mock_trainer)")
    endif()
    if (NOT DYNAMICAL_ONNX)
        list(FILTER test_sources EXCLUDE REGEX "/tests/onnx/")
    endif()
    add_executable(dynamical_tests ${test_sources})
    target_include_directories(dynamical_tests PRIVATE tests)
    target_link_libraries(dynamical_tests PRIVATE dynamical_sim doctest::doctest imgdiff_core)
    if (DYNAMICAL_ONNX)
        target_link_libraries(dynamical_tests PRIVATE dynamical_onnx)
        copy_onnxruntime(dynamical_tests)
    endif()
    if (DYNAMICAL_TRAIN)
        target_link_libraries(dynamical_tests PRIVATE dynamical_train_core)
        target_compile_definitions(dynamical_tests PRIVATE DYNAMICAL_TRAIN_EXE="$<TARGET_FILE:dynamical_train>")
        add_dependencies(dynamical_tests dynamical_train)
    endif()

    # doctest test suites (unit, scenario, protocol) become CTest labels
    doctest_discover_tests(dynamical_tests ADD_LABELS 1 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    if (DYNAMICAL_TRAIN AND MSVC)
        get_filename_component(MSVC_BIN "${CMAKE_LINKER}" DIRECTORY)
        find_program(DUMPBIN dumpbin HINTS "${MSVC_BIN}")
        add_test(NAME "T1.2 dynamical_train imports no vulkan-1, SDL2 or openxr_loader DLL"
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:dynamical_train> -DDUMPBIN=${DUMPBIN} -P ${CMAKE_SOURCE_DIR}/tests/import_table.cmake)
        set_tests_properties("T1.2 dynamical_train imports no vulkan-1, SDL2 or openxr_loader DLL" PROPERTIES LABELS unit)
    endif()

    if (DYNAMICAL_TRAIN)
        add_test(NAME "T1.7 dynamical_train from another directory writes relative outputs there and no config.6.json"
            COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:dynamical_train> -DWORK=${CMAKE_BINARY_DIR}/artifacts/P1/train_cwd -P ${CMAKE_SOURCE_DIR}/tests/train_cwd.cmake)
        set_tests_properties("T1.7 dynamical_train from another directory writes relative outputs there and no config.6.json" PROPERTIES LABELS scenario)

        # [slow] the real trainer, when build/venv holds the fork's ML-Agents (tools/setup_venv.py)
        set(VENV_PYTHON ${CMAKE_BINARY_DIR}/venv/Scripts/python.exe)
        if (EXISTS ${VENV_PYTHON})
            add_test(NAME "L3.1 mlagents-learn trains 2000 steps against TrainingEnv.exe mode=null"
                COMMAND ${VENV_PYTHON} ${CMAKE_SOURCE_DIR}/tools/train_smoke.py --steps 2000
                    --env $<TARGET_FILE_DIR:dynamical_train>/${DYNAMICAL_TRAIN_NAME} --results ${CMAKE_BINARY_DIR}/artifacts/P3/smoke)
            set_tests_properties("L3.1 mlagents-learn trains 2000 steps against TrainingEnv.exe mode=null" PROPERTIES LABELS slow RUN_SERIAL TRUE)
        endif()
    endif()

    # Visual and probe tests run dynamical: they need a GPU (label gpu, not run in CI)
    if (DYNAMICAL_BUILD_VIEWER)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(VISUAL_TEST ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/visual_test.py)
        set(VISUAL_EXES --exe $<TARGET_FILE:dynamical> --imgdiff $<TARGET_FILE:imgdiff>)
        set(ARTIFACTS ${CMAKE_BINARY_DIR}/artifacts)
        set(CAPTURES ${CMAKE_SOURCE_DIR}/tests/capture)

        add_test(NAME "V0.1 base captures are deterministic"
            COMMAND ${VISUAL_TEST} repeat ${VISUAL_EXES} --script ${CAPTURES}/base.capture.json --out ${ARTIFACTS}/P0/repeat --mae 0.1)
        add_test(NAME "V0.2 base captures match the goldens"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/base.capture.json --out ${ARTIFACTS}/P0/captures)
        add_test(NAME "P0 check: imgdiff fails on a capture with the camera moved by 0.1 m"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/base_moved.capture.json --out ${ARTIFACTS}/P0/moved --expect-fail)
        add_test(NAME "S0.1 base probe is deterministic"
            COMMAND ${VISUAL_TEST} probe --exe $<TARGET_FILE:dynamical> --out ${ARTIFACTS}/P0/probe --runs 2 --tol 1e-6
                -- probe_entities=RigidBody probe_every=60 ticks=900)
        add_test(NAME "V1.1 base scene captures match the P0 goldens"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/base.capture.json --out ${ARTIFACTS}/P1/captures)
        add_test(NAME "S1.1 base scene probe matches the P0 probe"
            COMMAND ${VISUAL_TEST} probe --exe $<TARGET_FILE:dynamical> --out ${ARTIFACTS}/P1/probe --runs 1 --tol 1e-5
                --golden ${CMAKE_SOURCE_DIR}/tests/golden/base_probe.csv -- scene=base probe_entities=RigidBody probe_every=60 ticks=900)
        add_test(NAME "P1 check: scripted input flies the camera, throws a box and toggles the grass"
            COMMAND ${VISUAL_TEST} shots --exe $<TARGET_FILE:dynamical> --script ${CAPTURES}/base_interactive.capture.json --out ${ARTIFACTS}/P1/interactive)
        add_test(NAME "V2.1 test_boxes_view at frame 300 matches its golden"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/worlds_boxes.capture.json --out ${ARTIFACTS}/P2/captures)
        add_test(NAME "V2.2 base captures after the Jolt 5.6 bump match the documented re-baseline"
            COMMAND ${VISUAL_TEST} golden ${VISUAL_EXES} --script ${CAPTURES}/base.capture.json --out ${ARTIFACTS}/P2/base)
        add_test(NAME "P2 check: scripted clicks pause and single-step WorldStepper"
            COMMAND ${VISUAL_TEST} shots --exe $<TARGET_FILE:dynamical> --script ${CAPTURES}/worlds_stepper.capture.json --out ${ARTIFACTS}/P2/stepper)
        set_tests_properties(
            "V2.1 test_boxes_view at frame 300 matches its golden"
            "V2.2 base captures after the Jolt 5.6 bump match the documented re-baseline"
            "P2 check: scripted clicks pause and single-step WorldStepper"
            "P1 check: scripted input flies the camera, throws a box and toggles the grass"
            "V1.1 base scene captures match the P0 goldens"
            "S1.1 base scene probe matches the P0 probe"
            "V0.1 base captures are deterministic"
            "V0.2 base captures match the goldens"
            "P0 check: imgdiff fails on a capture with the camera moved by 0.1 m"
            "S0.1 base probe is deterministic"
            PROPERTIES LABELS gpu RUN_SERIAL TRUE WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    endif()
endif()

### TOOLS
if (DYNAMICAL_BUILD_TOOLS)
    # Bakes resources/raycast_data.bin, the lookup texture used by the grass shader
    add_executable(raycast tools/raycast/main.cpp)
    target_include_directories(raycast PRIVATE "./src" ${CEREAL_INCLUDE_DIRS})
    target_link_libraries(raycast PRIVATE glm::glm)
endif()
