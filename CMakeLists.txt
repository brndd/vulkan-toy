cmake_minimum_required(VERSION 3.24)
project(vkeng)

set(CMAKE_CXX_STANDARD 17)

#Set gcc flags
set(CMAKE_CXX_FLAGS "-Wall -pipe -Werror=format-security -g")
set(CMAKE_CXX_FLAGS_DEBUG "-Og") #debugging-friendly optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

#Enable #ifdef DEBUG macros
if (CMAKE_BUILD_TYPE MATCHES Debug)
    add_definitions(-DDEBUG)
endif()

set(BUILD_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BUILD_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BUILD_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BUILD_DIR})

#Include SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

#Include 3rd party source libs with the SYSTEM keyword
#this silences warnings from them and lets us #include them with the bracket notation rather than quotes
include_directories(SYSTEM "src/3rd_party")

#Include Vulkan
find_package(Vulkan REQUIRED)

#Find glslc
find_program(GLSLC glslc HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

#Collect shader files
file(GLOB_RECURSE GLSL_SOURCE_FILES CONFIGURE_DEPENDS "src/shaders/*.frag" "src/shaders/*.vert" "src/shaders/*.comp")

foreach(GLSL ${GLSL_SOURCE_FILES})
    message(STATUS "BUILDING SHADER")
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${BUILD_DIR}/shaders/${FILE_NAME}.spv") #Compile under bin/shaders/
    message(STATUS ${GLSL})
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLC} -O ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

#Add shader compilation target
add_custom_target(shaders DEPENDS ${SPIRV_BINARY_FILES})

#Add main compilation target
add_executable(vkeng src/main.cpp src/vk_engine.cpp src/vk_engine.h src/vk_initializers.cpp src/vk_initializers.h src/vk_types.h src/3rd_party/vk_mem_alloc.cpp src/3rd_party/stb_image.cpp src/vk_mesh.cpp src/vk_mesh.h src/3rd_party/tiny_obj_loader.cpp src/3rd_party/stb_image.cpp src/vk_descriptors.cpp src/vk_descriptors.h)
target_link_libraries(vkeng ${Vulkan_LIBRARIES} ${SDL2_LIBRARIES}) #Depends on SDL2 and Vulkan
add_dependencies(vkeng shaders) #Depends on shaders being compiled

#Symlink data into the build directory
add_custom_command(TARGET vkeng POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_SOURCE_DIR}/data"
        "${BUILD_DIR}/data")
add_custom_target(symlink_data ALL DEPENDS "${CMAKE_BINARY_DIR}/data")
