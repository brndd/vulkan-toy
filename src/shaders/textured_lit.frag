#version 460

layout (location=0) in vec3 inColor;
layout (location=1) in vec2 texCoord;
layout (location=2) in vec3 fragPos;
layout (location=3) in vec3 normal;
layout (location=4) in vec3 viewPos;

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


layout (set=2, binding=0) uniform sampler2D tex1;

//Returns the specular component only
//lightColor.w = exponent
vec3 blinnPhong(vec3 lightDir, vec3 normal, vec4 lightColor) {
    vec3 V = normalize(viewPos - fragPos);
    vec3 H = normalize(lightDir + V);
    float HdotN = max(0.0, dot(H, normal));
    HdotN = pow(HdotN, lightColor.w);

    return lightColor.xyz * HdotN;
}

void main() {
    vec3 color = texture(tex1, texCoord).xyz;
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
    for (int i = 0; i < 3; i++) {
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
