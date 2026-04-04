#version 460 core
layout(location = 0) in vec3 aPos;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkPos;
uniform vec3 chunkSize;

void main()
{
    vec3 worldPos = chunkPos + (aPos * chunkSize);
    gl_Position = projection * view * vec4(worldPos, 1.0);
}
