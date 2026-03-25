#version 410
in vec3 vOut_worldNormal;
in vec3 vOut_worldPosition;
in vec4 vOut_color;

out vec4 FragColor;

uniform samplerCube envMap;
uniform vec3 cameraPosition;
uniform float reflectivity;

uniform vec3 globalAmbientColor;
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
    vec3 normal = normalize(vOut_worldNormal);
    vec3 viewDir = normalize(vOut_worldPosition - cameraPosition);
    vec3 R = reflect(viewDir, normal);

    vec4 reflectionColor = texture(envMap, R);

    // Fresnel: edges reflect more (Schlick approximation)
    float cosTheta = max(dot(normal, -viewDir), 0.0);
    float F0 = 0.95; // high F0 for metal
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);

    // Silver metallic tint applied to reflection
    vec3 metalTint = vec3(0.95, 0.93, 0.88);

    // Compute specular highlights from lights
    vec3 specTotal = vec3(0.0);
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

        // Tight specular (Blinn-Phong, high shininess for sharp highlights)
        vec3 halfDir = normalize(lightDir - viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 256.0);

        specTotal += lightColors[i].rgb * spec * lightIntensities[i] * attenuation;
    }

    // Tinted reflection + bright specular highlights on top
    vec3 metalReflection = reflectionColor.rgb * metalTint;
    vec3 finalColor = mix(metalTint * 0.05, metalReflection, fresnel * reflectivity);
    finalColor += specTotal * 1.5;

    FragColor = vec4(finalColor, 1.0);
}
