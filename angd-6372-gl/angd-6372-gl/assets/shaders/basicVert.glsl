#version 410
layout(location = 0) in vec3 vIn_position;
layout(location = 1) in vec4 vIn_color;
layout(location = 2) in vec2 vIn_texcoord;
layout(location = 3) in vec3 vIn_normal;

out vec4 vOut_color;
out vec2 vOut_texcoord;
out vec3 vOut_worldNormal;
out vec3 vOut_worldPosition;

uniform mat4 projectionMatrix;
uniform mat4 viewMatrix;
uniform mat4 modelMatrix;
uniform vec4 color;

uniform vec3 globalAmbientColor;

#define MAX_LIGHTS 8
uniform int numLights;
uniform vec4 lightPositions[MAX_LIGHTS];
uniform vec4 lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];

void main()
{
    vOut_worldNormal = normalize(mat3(modelMatrix) * vIn_normal);
    
    vOut_worldPosition = (modelMatrix * vec4(vIn_position, 1.0)).xyz;
    
    vOut_color = vIn_color * color;
    vOut_texcoord = vIn_texcoord;
    
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vIn_position, 1.0);
}