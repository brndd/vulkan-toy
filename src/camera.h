#ifndef VKENG_CAMERA_H
#define VKENG_CAMERA_H

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <SDL.h>
#include <iostream>

enum class Direction {
    FORWARD = 1 << 0,
    BACKWARD = 1 << 1,
    LEFT = 1 << 2,
    RIGHT = 1 << 3
};

//Default values
constexpr float YAW = -90.0f;
constexpr float PITCH = 0.0f;
constexpr float SPEED = 10.0f;
constexpr float SENSITIVITY = 0.1f;
constexpr float FOV = 70.0f;

class camera {
public:
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp;
    float m_yaw;
    float m_pitch;
    float m_speed;
    float m_sensitivity;
    float m_fov;

    camera(glm::vec3 position = glm::vec3(0.0f, 10.0f, 10.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) : m_front(glm::vec3(0.0f, 0.0f, -1.0f)), m_speed(SPEED), m_sensitivity(SENSITIVITY), m_fov(FOV) {
        m_position = position;
        m_worldUp = up;
        m_yaw = yaw;
        m_pitch = pitch;
        updateCameraVectors();
    }

    glm::mat4 getViewMatrix() {
        return glm::lookAt(m_position, m_position + m_front, m_up);
    }

    void processKeyboard(float deltaTime) {
        if (deltaTime == 0.0f) return;
        float velocity = m_speed * deltaTime;
        glm::vec3 direction = glm::vec3(0.0f);

        const Uint8* heldKeys = SDL_GetKeyboardState(NULL);
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_W]) {
            direction += m_front;
        }
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_S]) {
            direction -= m_front;
        }
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_A]) {
            direction -= m_right;
        }
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_D]) {
            direction += m_right;
        }
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_SPACE]) {
            direction += m_worldUp;
        }
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_LCTRL]) {
            direction -= m_worldUp;
        }
        if (heldKeys[SDL_Scancode::SDL_SCANCODE_LSHIFT]) {
            velocity *= 5;
        }

        if (direction == glm::vec3(0.0f, 0.0f, 0.0f)) {
            return;
        }

        direction = glm::normalize(direction);
        direction *= velocity;
        m_position += direction;
    }

    void processMouseMovement(float xoffset, float yoffset) {
        xoffset *= m_sensitivity;
        yoffset *= m_sensitivity;
        m_yaw += xoffset;
        m_pitch -= yoffset;

        if (m_pitch > 89.0f) {
            m_pitch = 89.0f;
        }
        if (m_pitch < -89.0f) {
            m_pitch = -89.0f;
        }

        if (m_yaw > 180.0f) {
            m_yaw -= 360.0f;
        }
        else if (m_yaw < -180.0f) {
            m_yaw += 360.0f;
        }

        updateCameraVectors();
    }

private:
    void updateCameraVectors() {
        glm::vec3 front;
        front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        front.y = sin(glm::radians(m_pitch));
        front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        m_front = glm::normalize(front);
        m_right = glm::normalize(glm::cross(m_front, m_worldUp));
        m_up = glm::normalize(glm::cross(m_right, m_front));
    }
};


#endif //VKENG_CAMERA_H
