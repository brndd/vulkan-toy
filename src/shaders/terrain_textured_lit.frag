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

struct ObjectData{
    mat4 model;
};

struct PointLightData{
    vec4 lightPosition;
    vec4 lightColor;
};

//All light matrices
layout(std140, set = 1, binding = 1) readonly buffer LightBuffer{
    PointLightData lights[];
} lightBuffer;

layout(push_constant) uniform per_object {
    layout(offset=80) int texIdx;
} texData;

layout (set=2, binding=0) uniform sampler2D tex1[5];

//Returns the specular component only
//lightColor.w = exponent
vec3 blinnPhong(vec3 lightDir, vec3 normal, vec4 lightColor) {
    vec3 V = normalize(viewPos - fragPos);
    vec3 H = normalize(lightDir + V);
    float HdotN = max(0.0, dot(H, normal));
    HdotN = pow(HdotN, lightColor.w);

    return lightColor.xyz * HdotN;
}

const float tilingFactor = 8.0f; //repeat texture 8 times for each "chunk"

void main() {
    vec2 tiledTexCoord = texCoord * tilingFactor;
    vec3 color = vec3(0.0f);
    if (worldHeight < 30.0f) { //all grass
        color = texture(tex1[0], tiledTexCoord).xyz;
    }
    else if (worldHeight < 50.0f) { //mix of grass and rock
        vec3 grass = texture(tex1[0], tiledTexCoord).xyz;
        vec3 rock = texture(tex1[1], tiledTexCoord).xyz;
        color = mix(grass, rock, (worldHeight - 30.0f) / 20.0f);
    }
    else if (worldHeight < 70.0f) { //all rock
        color = texture(tex1[1], tiledTexCoord).xyz;
    }
    else if (worldHeight < 90.0f) { //mix of rock and snow
        vec3 rock = texture(tex1[1], tiledTexCoord).xyz;
        vec3 snow = texture(tex1[2], tiledTexCoord).xyz;
        color = mix(rock, snow, (worldHeight - 70.0f) / 20.0f);
    }
    else { //all snow
        color = texture(tex1[2], tiledTexCoord).xyz;
    }

    //vec3 color = texture(tex1[texData.texIdx], tiledTexCoord).xyz;
    //vec3 color = vec3(1.0f);
    vec3 lights = vec3(0.0f);
    //Calculate sunlight
    {
        lights += sceneData.ambientColor.xyz;

        vec3 sunDir = sceneData.sunlightDirection.xyz;
        vec4 sunCol = sceneData.sunlightColor;
        float sunCos = dot(sunDir, normal);
        vec3 sunDiffuse = max(0.0, sunCos) * sunCol.xyz;
        vec3 sunSpecular = blinnPhong(sunDir, normal, sunCol);

        lights += sunDiffuse + sunSpecular;
    }

    //Calculate dynamic lights
    for (int i = 0; i < 0; i++) {
        PointLightData light = lightBuffer.lights[i];
        vec3 pointPos = light.lightPosition.xyz;
        vec3 pointDir = normalize(pointPos - fragPos);
        float pointDist = distance(fragPos, pointPos);
        vec4 pointColor = vec4(light.lightColor.xyz * (1/pow(pointDist, 2.0f)), light.lightColor.w);
        float pointCos = dot(pointDir, normal);
        vec3 pointDiffuse = max(0.0, pointCos) * pointColor.xyz;
        vec3 pointSpecular = blinnPhong(pointDir, normal, pointColor);
        lights += pointDiffuse + pointSpecular;
    }
    color *= lights;

    outColor = vec4(color, 1.0f);
}
