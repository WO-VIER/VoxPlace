#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in float aAO;

out vec3 fragColor;
out float fragAO;
out vec3 fragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    fragPos = worldPos.xyz;
    
    gl_Position = projection * view * worldPos;
    fragColor = aColor;
    fragAO = aAO;
}
