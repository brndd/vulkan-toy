#ifndef VKENG_VK_ENGINE_H
#define VKENG_VK_ENGINE_H


#include "vk_types.h"

class VulkanEngine {
public:
    bool _isInitialized{false};
    int _frameNumber {0};

    VkExtent2D _windowExtent{640, 480};

    struct SDL_Window* _window{nullptr};

    //Initialize engine
    void init();

    //Shut down and clean up
    void cleanup();

    //Draw loop
    void draw();

    //Main loop
    void run();

};


#endif //VKENG_VK_ENGINE_H
