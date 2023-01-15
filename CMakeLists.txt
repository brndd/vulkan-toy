cmake_minimum_required(VERSION 3.24)
project(vkeng)

set(CMAKE_CXX_STANDARD 17)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS glslc )
include_directories(${SDL2_INCLUDE_DIRS})

add_executable(vkeng src/main.cpp src/vk_engine.cpp src/vk_engine.h src/vk_initializers.cpp src/vk_initializers.h src/vk_types.h src/lib/vk_mem_alloc.cpp src/lib/vk_mem_alloc.h)
target_link_libraries(vkeng ${Vulkan_LIBRARIES} ${SDL2_LIBRARIES})
