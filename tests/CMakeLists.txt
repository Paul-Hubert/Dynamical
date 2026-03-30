cmake_minimum_required(VERSION 3.10)

# Find GTest
find_package(GTest REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/tests)

# Create test executable
add_executable(dynamical_tests
    # Test files
    llm/test_llm_client.cpp
    llm/test_llm_manager.cpp
    llm/test_response_parser.cpp
    llm/test_prompt_builder.cpp
    llm/test_decision_cache.cpp
    # Phase 3 implementation files
    ../src/llm/llm_client.cpp
    ../src/llm/llm_manager.cpp
    ../src/llm/response_parser.cpp
    ../src/llm/prompt_builder.cpp
    ../src/llm/decision_cache.cpp
    # AI dependencies
    ../src/ai/action_registry.cpp
    ../src/ai/action_registry_init.cpp
    ../src/ai/memory/ai_memory.cpp
)

# Link libraries
target_link_libraries(dynamical_tests PRIVATE GTest::gtest GTest::gtest_main ai-sdk-cpp)

# Link other necessary libraries
find_package(Vulkan REQUIRED)
target_link_libraries(dynamical_tests PRIVATE Vulkan::Vulkan)

find_package(SDL2 REQUIRED)
target_include_directories(dynamical_tests PRIVATE ${SDL2_INCLUDE_DIR})
target_link_libraries(dynamical_tests PRIVATE ${SDL2_LIBRARIES})

find_path(CEREAL_INCLUDE_DIRS "cereal")
target_include_directories(dynamical_tests PRIVATE ${CEREAL_INCLUDE_DIRS})

find_path(GLM_INCLUDE_DIR "glm")
target_include_directories(dynamical_tests PRIVATE ${GLM_INCLUDE_DIRS})

find_path(TASKFLOW_INCLUDE_DIR "taskflow")
target_include_directories(dynamical_tests PRIVATE ${TASKFLOW_INCLUDE_DIRS})

target_include_directories(dynamical_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/ai-sdk-cpp/include)
target_include_directories(dynamical_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/ai-sdk-cpp/third_party/nlohmann_json_patched/include)
target_include_directories(dynamical_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/FastNoise2/include)

# Ensure ai-sdk-cpp is properly configured for tests
target_compile_definitions(dynamical_tests PRIVATE AI_SDK_HAS_OPENAI=1 AI_SDK_HAS_ANTHROPIC=1)

# Add test target
add_test(NAME AllTests COMMAND dynamical_tests)
