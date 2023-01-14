#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vk_types.h"
#include "vk_initializers.h"

void VulkanEngine::init() {
    //Initialize SDL window
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    //Create window
    _window = SDL_CreateWindow(
            "vkeng",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            _windowExtent.width,
            _windowExtent.height,
            window_flags
            );

    _isInitialized = true;
}

void VulkanEngine::cleanup() {
    if (_isInitialized) {
        SDL_DestroyWindow(_window);
    }
}

void VulkanEngine::draw() {

}

void VulkanEngine::run() {
    SDL_Event e;
    bool bQuit = false;

    while (!bQuit) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) bQuit = true;
        }

        draw();
    }
}
