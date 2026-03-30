#ifndef CHUNK_GPU_RESOURCES_H
#define CHUNK_GPU_RESOURCES_H

#include <glad/glad.h>

#include <cstdint>
#include <vector>

struct ChunkGpuResources
{
	GLuint ssbo = 0;
	GLuint vao = 0;
	std::vector<uint32_t> packedFacesCpu;
};

#endif
