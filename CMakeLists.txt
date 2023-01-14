cmake_minimum_required(VERSION 3.24)
project(vkeng)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_executable(vkeng src/main.cpp src/vk_engine.cpp src/vk_engine.h src/vk_initializers.cpp src/vk_initializers.h src/vk_types.h)
target_link_libraries(vkeng ${SDL2_LIBRARIES})
