#version 460

layout (location=0) in vec3 inColor;
layout (location=1) in vec2 texCoord;
layout (location=2) in vec3 fragPos;
layout (location=3) in vec3 normal;
layout (location=4) in vec3 viewPos;
layout (location=5) in float worldHeight;

layout (location=0) out vec4 outColor;

layout (set=0, binding=1) uniform SceneData{
    vec4 fogColor;
    vec4 fogDistances;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} sceneData;

void main() {
    vec3 lowColor = vec3(0.0, 0.0, 1.0);
    vec3 highColor = vec3(1.0, 0.0, 0.0);
    float minHeight = 0.0f;
    float maxHeight = 10.0f;
    float normalizedHeight = clamp((worldHeight - minHeight) / (maxHeight - minHeight), 0.0, 1.0);
    vec3 color = mix(lowColor, highColor, normalizedHeight);
    outColor = vec4(color, 1.0);
}
