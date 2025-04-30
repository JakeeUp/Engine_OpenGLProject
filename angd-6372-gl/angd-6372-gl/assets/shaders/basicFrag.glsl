#version 410
in vec4 vOut_color;
in vec2 vOut_texcoord;
in vec3 vOut_worldNormal;
in vec3 vOut_worldPosition;

out vec4 FragColor;

uniform sampler2D textureSampler;
uniform bool useTexture;

// Global ambient lighting
uniform vec3 globalAmbientColor;

// Light uniforms (up to 8 lights supported)
#define MAX_LIGHTS 8
uniform int numLights;
uniform vec4 lightPositions[MAX_LIGHTS];
uniform vec4 lightColors[MAX_LIGHTS];
uniform float lightIntensities[MAX_LIGHTS];
uniform float lightAttenuationConstants[MAX_LIGHTS];
uniform float lightAttenuationLinears[MAX_LIGHTS];
uniform float lightAttenuationQuadratics[MAX_LIGHTS];

void main()
{
    vec4 baseColor;
    if (useTexture) {
        baseColor = texture(textureSampler, vOut_texcoord) * vOut_color;
    } else {
        baseColor = vOut_color;
    }
    
    vec3 lightTotal = globalAmbientColor;
    
    vec3 normal = normalize(vOut_worldNormal);
    
    for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
        if (lightIntensities[i] <= 0.0) continue;
        
        vec3 lightDir;
        float attenuation = 1.0;
        
        if (lightPositions[i].w > 0.5) {
            vec3 lightToVertex = vOut_worldPosition - lightPositions[i].xyz;
            float distance = length(lightToVertex);
            lightDir = -normalize(lightToVertex);
            
            attenuation = 1.0 / (
                lightAttenuationConstants[i] + 
                lightAttenuationLinears[i] * distance + 
                lightAttenuationQuadratics[i] * distance * distance
            );
        } else {
            lightDir = normalize(lightPositions[i].xyz);
        }
        
        float diffuse = max(0.0, dot(normal, lightDir));
        
        lightTotal += lightColors[i].rgb * diffuse * lightIntensities[i] * attenuation;
    }
    
    FragColor = vec4(baseColor.rgb * lightTotal, baseColor.a);
}