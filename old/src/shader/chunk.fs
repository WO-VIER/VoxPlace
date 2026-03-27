#version 330 core
out vec4 FragColor;

in vec3 fragColor;
in float fragAO;
in vec3 fragPos;

uniform vec3 cameraPos;

// Fog settings
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;

void main()
{
    // Appliquer l'Ambient Occlusion
    vec3 color = fragColor * fragAO;
    
    // Distance fog
    float dist = length(fragPos - cameraPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    
    color = mix(color, fogColor, fogFactor);
    
    FragColor = vec4(color, 1.0);
}
