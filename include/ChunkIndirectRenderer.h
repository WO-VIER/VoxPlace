#ifndef CHUNK_INDIRECT_RENDERER_H
#define CHUNK_INDIRECT_RENDERER_H

#include <Chunk2.h>

#include <cstdint>
#include <vector>

struct ChunkDrawArraysIndirectCommand
{
	uint32_t count = 0;
	uint32_t instanceCount = 1;
	uint32_t first = 0;
	uint32_t baseInstance = 0;
};

struct ChunkIndirectDrawInfoGpu
{
	uint32_t faceOffset = 0;
	int32_t chunkX = 0;
	int32_t chunkZ = 0;
	int32_t padding = 0;
};

class ChunkIndirectRenderer
{
public:
	GLuint facesSsbo = 0;
	GLuint drawInfoSsbo = 0;
	GLuint indirectBuffer = 0;
	GLuint vao = 0;
	size_t drawCount = 0;
	size_t faceCount = 0;

	void init()
	{
		if (vao == 0)
		{
			glGenVertexArrays(1, &vao);
		}
		if (facesSsbo == 0)
		{
			glGenBuffers(1, &facesSsbo);
		}
		if (drawInfoSsbo == 0)
		{
			glGenBuffers(1, &drawInfoSsbo);
		}
		if (indirectBuffer == 0)
		{
			glGenBuffers(1, &indirectBuffer);
		}
	}

	void cleanup()
	{
		if (facesSsbo != 0)
		{
			glDeleteBuffers(1, &facesSsbo);
		}
		if (drawInfoSsbo != 0)
		{
			glDeleteBuffers(1, &drawInfoSsbo);
		}
		if (indirectBuffer != 0)
		{
			glDeleteBuffers(1, &indirectBuffer);
		}
		if (vao != 0)
		{
			glDeleteVertexArrays(1, &vao);
		}

		facesSsbo = 0;
		drawInfoSsbo = 0;
		indirectBuffer = 0;
		vao = 0;
		drawCount = 0;
		faceCount = 0;
	}

	void uploadVisibleChunks(const std::vector<Chunk2 *> &visibleChunks)
	{
		std::vector<uint32_t> aggregatedFaces;
		std::vector<ChunkIndirectDrawInfoGpu> drawInfos;
		std::vector<ChunkDrawArraysIndirectCommand> drawCommands;

		size_t totalWords = 0;
		for (const Chunk2 *chunk : visibleChunks)
		{
			if (chunk == nullptr)
			{
				continue;
			}
			totalWords += chunk->packedFaces().size();
		}

		aggregatedFaces.reserve(totalWords);
		drawInfos.reserve(visibleChunks.size());
		drawCommands.reserve(visibleChunks.size());

		uint32_t faceOffset = 0;
		for (const Chunk2 *chunk : visibleChunks)
		{
			if (chunk == nullptr)
			{
				continue;
			}
			if (chunk->faceCount == 0)
			{
				continue;
			}
			if (chunk->packedFaces().empty())
			{
				continue;
			}

			ChunkIndirectDrawInfoGpu drawInfo;
			drawInfo.faceOffset = faceOffset;
			drawInfo.chunkX = chunk->chunkX;
			drawInfo.chunkZ = chunk->chunkZ;
			drawInfos.push_back(drawInfo);

			ChunkDrawArraysIndirectCommand command;
			command.count = chunk->faceCount * 6;
			drawCommands.push_back(command);

			aggregatedFaces.insert(
				aggregatedFaces.end(),
				chunk->packedFaces().begin(),
				chunk->packedFaces().end());
			faceOffset += chunk->faceCount;
		}

		drawCount = drawCommands.size();
		faceCount = faceOffset;

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, facesSsbo);
		glBufferData(
			GL_SHADER_STORAGE_BUFFER,
			aggregatedFaces.size() * sizeof(uint32_t),
			aggregatedFaces.data(),
			GL_STREAM_DRAW);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawInfoSsbo);
		glBufferData(
			GL_SHADER_STORAGE_BUFFER,
			drawInfos.size() * sizeof(ChunkIndirectDrawInfoGpu),
			drawInfos.data(),
			GL_STREAM_DRAW);

		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
		glBufferData(
			GL_DRAW_INDIRECT_BUFFER,
			drawCommands.size() * sizeof(ChunkDrawArraysIndirectCommand),
			drawCommands.data(),
			GL_STREAM_DRAW);
	}

	void draw() const
	{
		if (drawCount == 0)
		{
			return;
		}

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, facesSsbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, drawInfoSsbo);
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
		glBindVertexArray(vao);
		glMultiDrawArraysIndirect(GL_TRIANGLES, nullptr, static_cast<GLsizei>(drawCount), 0);
	}
};

#endif
