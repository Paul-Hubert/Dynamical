cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 17)

project(raycast)

include_directories("../../3rdparty/")
file(GLOB_RECURSE raycast_src ./src/main.cpp ./src/*.cpp ./src/*.h)

add_executable(raycast ${raycast_src})

find_package(cereal CONFIG REQUIRED)
target_link_libraries(raycast PRIVATE cereal)

add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/raycast_data.bin
  COMMAND raycast ${PROJECT_SOURCE_DIR}/raycast_data.bin
  DEPENDS raycast
)

add_custom_target(raycast_run ALL
   DEPENDS ${PROJECT_SOURCE_DIR}/raycast_data.bin
)
