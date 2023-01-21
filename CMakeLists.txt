cmake_minimum_required(VERSION 3.24)
project(vkeng)

set(CMAKE_CXX_STANDARD 17)

if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(Vulkan REQUIRED)

find_program(GLSLC glslc HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

file(GLOB_RECURSE GLSL_SOURCE_FILES "src/shaders/*.frag" "src/shaders/*.vert" "src/shaders/*.comp")

foreach(GLSL ${GLSL_SOURCE_FILES})
    message(STATUS "BUILDING SHADER")
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_SOURCE_DIR}/src/shaders/${FILE_NAME}.spv")
    message(STATUS ${GLSL})
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLC} -O ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_executable(vkeng src/main.cpp src/vk_engine.cpp src/vk_engine.h src/vk_initializers.cpp src/vk_initializers.h src/vk_types.h src/lib/vk_mem_alloc.cpp src/lib/vk_mem_alloc.h)
target_link_libraries(vkeng ${Vulkan_LIBRARIES} ${SDL2_LIBRARIES})
add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(vkeng shaders)
