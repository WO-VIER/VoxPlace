#ifndef CHUNK_INDIRECT_RENDERER_H
#define CHUNK_INDIRECT_RENDERER_H

#include <ClientChunk.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <unordered_map>
#include <vector>

struct ChunkDrawArraysIndirectCommand
{
	uint32_t count = 0;
	uint32_t instanceCount = 1;
	uint32_t first = 0;
	uint32_t baseInstance = 0;
};

struct ChunkFaceInstanceGpu
{
	uint32_t word0 = 0;
	uint32_t word1 = 0;
	int32_t chunkX = 0;
	int32_t chunkZ = 0;
};

class ChunkIndirectRenderer
{
public:
	struct ArenaEntry
	{
		size_t faceOffset = 0;
		size_t reservedFaceCount = 0;
		uint32_t faceCount = 0;
		ClientChunk *chunk = nullptr;
	};

	GLuint facesVbo = 0;
	GLuint indirectBuffer = 0;
	GLuint vao = 0;
	size_t drawCount = 0;
	size_t faceCount = 0;
	size_t arenaFaceCapacity = 0;
	size_t arenaReservedFaces = 0;
	size_t arenaUsedFaces = 0;
	size_t largestFreeFaceSpan = 0;
	uint64_t arenaVersion = 0;
	uint64_t cachedArenaVersion = std::numeric_limits<uint64_t>::max();
	uint64_t drawDataBuildCount = 0;
	bool lastBuildReused = false;

	void init(size_t initialFaceCapacity = 0)
	{
		cleanup();

		if (initialFaceCapacity == 0)
		{
			initialFaceCapacity = 256 * 1024;
		}
		arenaFaceCapacity = initialFaceCapacity;

		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &facesVbo);
		glGenBuffers(1, &indirectBuffer);

		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, facesVbo);
		glBufferData(
			GL_ARRAY_BUFFER,
			arenaFaceCapacity * sizeof(ChunkFaceInstanceGpu),
			nullptr,
			GL_DYNAMIC_DRAW);
		setupVertexAttributes();
		glBindVertexArray(0);
	}

	void cleanup()
	{
		if (facesVbo != 0)
		{
			glDeleteBuffers(1, &facesVbo);
		}
		if (indirectBuffer != 0)
		{
			glDeleteBuffers(1, &indirectBuffer);
		}
		if (vao != 0)
		{
			glDeleteVertexArrays(1, &vao);
		}

		facesVbo = 0;
		indirectBuffer = 0;
		vao = 0;
		drawCount = 0;
		faceCount = 0;
		arenaFaceCapacity = 0;
		arenaReservedFaces = 0;
		arenaUsedFaces = 0;
		largestFreeFaceSpan = 0;
		arenaVersion = 0;
		cachedArenaVersion = std::numeric_limits<uint64_t>::max();
		drawDataBuildCount = 0;
		lastBuildReused = false;
		entriesList.clear();
		entriesMap.clear();
		cachedVisibleKeys.clear();
	}

	void upsertChunk(ClientChunk &chunk)
	{
		int64_t key = chunkKey(chunk.storage.chunkX, chunk.storage.chunkZ);
		if (chunk.renderState.faceCount == 0 || chunk.packedFaces().empty())
		{
			removeChunk(key);
			return;
		}

		std::vector<ChunkFaceInstanceGpu> instances = buildFaceInstances(chunk);

		auto entryIt = entriesMap.find(key);
		if (entryIt != entriesMap.end())
		{
			ArenaEntry &entry = *entryIt->second;
			entry.chunk = &chunk;

			if (instances.size() <= entry.reservedFaceCount)
			{
				entry.faceCount = chunk.renderState.faceCount;
				writeInstances(entry.faceOffset, instances);
				recomputeArenaReservedFaces();
				markArenaDirty();
				return;
			}

			entriesList.erase(entryIt->second);
			entriesMap.erase(entryIt);
		}

		ensureSpaceFor(instances.size());
		insertChunkAllocation(key, chunk, instances);
	}

	void removeChunk(int64_t key)
	{
		auto entryIt = entriesMap.find(key);
		if (entryIt == entriesMap.end())
		{
			return;
		}

		entriesList.erase(entryIt->second);
		entriesMap.erase(entryIt);
		recomputeArenaReservedFaces();
		markArenaDirty();
	}

	void buildVisibleDrawData(const std::vector<ClientChunk *> &visibleChunks)
	{
		std::vector<int64_t> visibleKeys;
		std::vector<ChunkDrawArraysIndirectCommand> drawCommands;

		visibleKeys.reserve(visibleChunks.size());
		drawCommands.reserve(visibleChunks.size());

		faceCount = 0;
		for (ClientChunk *chunk : visibleChunks)
		{
			if (chunk == nullptr)
			{
				continue;
			}

			int64_t key = chunkKey(chunk->storage.chunkX, chunk->storage.chunkZ);
			auto entryIt = entriesMap.find(key);
			if (entryIt == entriesMap.end())
			{
				continue;
			}

			const ArenaEntry &entry = *entryIt->second;
			if (entry.faceCount == 0)
			{
				continue;
			}

			visibleKeys.push_back(key);

			ChunkDrawArraysIndirectCommand command;
			command.count = 6;
			command.instanceCount = entry.faceCount;
			command.first = 0;
			command.baseInstance = static_cast<uint32_t>(entry.faceOffset);
			drawCommands.push_back(command);

			faceCount += entry.faceCount;
		}

		if (cachedArenaVersion == arenaVersion && visibleKeys == cachedVisibleKeys)
		{
			drawCount = drawCommands.size();
			lastBuildReused = true;
			return;
		}

		drawCount = drawCommands.size();
		lastBuildReused = false;
		drawDataBuildCount++;
		cachedVisibleKeys = std::move(visibleKeys);
		cachedArenaVersion = arenaVersion;

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

		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
		glBindVertexArray(vao);
		glMultiDrawArraysIndirect(GL_TRIANGLES, nullptr, static_cast<GLsizei>(drawCount), 0);
	}

	void compact()
	{
		if (facesVbo == 0)
		{
			return;
		}

		GLuint newFacesVbo = 0;
		glGenBuffers(1, &newFacesVbo);
		glBindBuffer(GL_ARRAY_BUFFER, newFacesVbo);
		glBufferData(
			GL_ARRAY_BUFFER,
			arenaFaceCapacity * sizeof(ChunkFaceInstanceGpu),
			nullptr,
			GL_DYNAMIC_DRAW);

		size_t cursor = 0;
		for (ArenaEntry &entry : entriesList)
		{
			if (entry.chunk == nullptr)
			{
				continue;
			}

			std::vector<ChunkFaceInstanceGpu> instances = buildFaceInstances(*entry.chunk);
			entry.faceOffset = cursor;
			entry.reservedFaceCount = instances.size();
			entry.faceCount = entry.chunk->renderState.faceCount;

			glBufferSubData(
				GL_ARRAY_BUFFER,
				static_cast<GLintptr>(entry.faceOffset * sizeof(ChunkFaceInstanceGpu)),
				static_cast<GLsizeiptr>(instances.size() * sizeof(ChunkFaceInstanceGpu)),
				instances.data());
			cursor += entry.reservedFaceCount;
		}

		if (facesVbo != 0)
		{
			glDeleteBuffers(1, &facesVbo);
		}
		facesVbo = newFacesVbo;
		rebindFaceBuffer();
		recomputeArenaStats();
		markArenaDirty();
	}

private:
	using EntryList = std::list<ArenaEntry>;
	using EntryIterator = EntryList::iterator;

	EntryList entriesList;
	std::unordered_map<int64_t, EntryIterator> entriesMap;
	std::vector<int64_t> cachedVisibleKeys;

	void markArenaDirty()
	{
		arenaVersion++;
	}

	void setupVertexAttributes()
	{
		glEnableVertexAttribArray(0);
		glVertexAttribIPointer(
			0,
			1,
			GL_UNSIGNED_INT,
			sizeof(ChunkFaceInstanceGpu),
			reinterpret_cast<void *>(offsetof(ChunkFaceInstanceGpu, word0)));
		glVertexAttribDivisor(0, 1);

		glEnableVertexAttribArray(1);
		glVertexAttribIPointer(
			1,
			1,
			GL_UNSIGNED_INT,
			sizeof(ChunkFaceInstanceGpu),
			reinterpret_cast<void *>(offsetof(ChunkFaceInstanceGpu, word1)));
		glVertexAttribDivisor(1, 1);

		glEnableVertexAttribArray(2);
		glVertexAttribIPointer(
			2,
			1,
			GL_INT,
			sizeof(ChunkFaceInstanceGpu),
			reinterpret_cast<void *>(offsetof(ChunkFaceInstanceGpu, chunkX)));
		glVertexAttribDivisor(2, 1);

		glEnableVertexAttribArray(3);
		glVertexAttribIPointer(
			3,
			1,
			GL_INT,
			sizeof(ChunkFaceInstanceGpu),
			reinterpret_cast<void *>(offsetof(ChunkFaceInstanceGpu, chunkZ)));
		glVertexAttribDivisor(3, 1);
	}

	void rebindFaceBuffer()
	{
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, facesVbo);
		setupVertexAttributes();
		glBindVertexArray(0);
	}

	std::vector<ChunkFaceInstanceGpu> buildFaceInstances(const ClientChunk &chunk) const
	{
		const std::vector<uint32_t> &packedFaces = chunk.packedFaces();
		std::vector<ChunkFaceInstanceGpu> instances;
		instances.reserve(packedFaces.size() / 2);

		for (size_t wordIndex = 0; wordIndex + 1 < packedFaces.size(); wordIndex += 2)
		{
			ChunkFaceInstanceGpu instance;
			instance.word0 = packedFaces[wordIndex];
			instance.word1 = packedFaces[wordIndex + 1];
			instance.chunkX = chunk.storage.chunkX;
			instance.chunkZ = chunk.storage.chunkZ;
			instances.push_back(instance);
		}

		return instances;
	}

	void recomputeArenaReservedFaces()
	{
		size_t reservedFaces = 0;
		size_t usedFaces = 0;
		size_t largestGap = 0;
		size_t cursor = 0;
		for (const ArenaEntry &entry : entriesList)
		{
			if (entry.faceOffset > cursor)
			{
				size_t gap = entry.faceOffset - cursor;
				if (gap > largestGap)
				{
					largestGap = gap;
				}
			}
			reservedFaces += entry.reservedFaceCount;
			usedFaces += entry.faceCount;
			cursor = entry.faceOffset + entry.reservedFaceCount;
		}
		if (arenaFaceCapacity > cursor)
		{
			size_t gap = arenaFaceCapacity - cursor;
			if (gap > largestGap)
			{
				largestGap = gap;
			}
		}
		arenaReservedFaces = reservedFaces;
		arenaUsedFaces = usedFaces;
		largestFreeFaceSpan = largestGap;
	}

	void recomputeArenaStats()
	{
		recomputeArenaReservedFaces();
	}

	void writeInstances(size_t faceOffset, const std::vector<ChunkFaceInstanceGpu> &instances)
	{
		glBindBuffer(GL_ARRAY_BUFFER, facesVbo);
		glBufferSubData(
			GL_ARRAY_BUFFER,
			static_cast<GLintptr>(faceOffset * sizeof(ChunkFaceInstanceGpu)),
			static_cast<GLsizeiptr>(instances.size() * sizeof(ChunkFaceInstanceGpu)),
			instances.data());
	}

	void ensureSpaceFor(size_t requiredFaces)
	{
		while (!hasGapFor(requiredFaces))
		{
			compact();
			if (hasGapFor(requiredFaces))
			{
				return;
			}
			growArena(requiredFaces);
		}
	}

	bool hasGapFor(size_t requiredFaces) const
	{
		size_t cursor = 0;
		for (const ArenaEntry &entry : entriesList)
		{
			if (entry.faceOffset >= cursor + requiredFaces)
			{
				return true;
			}
			cursor = entry.faceOffset + entry.reservedFaceCount;
		}
		if (arenaFaceCapacity >= cursor + requiredFaces)
		{
			return true;
		}
		return false;
	}

	void growArena(size_t requiredFaces)
	{
		size_t newCapacity = arenaFaceCapacity;
		if (newCapacity == 0)
		{
			newCapacity = requiredFaces;
		}
		while (newCapacity < arenaReservedFaces + requiredFaces)
		{
			newCapacity *= 2;
			if (newCapacity == 0)
			{
				newCapacity = requiredFaces;
			}
		}

		GLuint newFacesVbo = 0;
		glGenBuffers(1, &newFacesVbo);
		glBindBuffer(GL_ARRAY_BUFFER, newFacesVbo);
		glBufferData(
			GL_ARRAY_BUFFER,
			newCapacity * sizeof(ChunkFaceInstanceGpu),
			nullptr,
			GL_DYNAMIC_DRAW);

		size_t cursor = 0;
		for (ArenaEntry &entry : entriesList)
		{
			if (entry.chunk == nullptr)
			{
				continue;
			}

			std::vector<ChunkFaceInstanceGpu> instances = buildFaceInstances(*entry.chunk);
			entry.faceOffset = cursor;
			entry.reservedFaceCount = instances.size();
			entry.faceCount = entry.chunk->renderState.faceCount;

			glBufferSubData(
				GL_ARRAY_BUFFER,
				static_cast<GLintptr>(entry.faceOffset * sizeof(ChunkFaceInstanceGpu)),
				static_cast<GLsizeiptr>(instances.size() * sizeof(ChunkFaceInstanceGpu)),
				instances.data());
			cursor += entry.reservedFaceCount;
		}

		if (facesVbo != 0)
		{
			glDeleteBuffers(1, &facesVbo);
		}
		facesVbo = newFacesVbo;
		arenaFaceCapacity = newCapacity;
		recomputeArenaReservedFaces();
		rebindFaceBuffer();
		markArenaDirty();
	}

	void insertChunkAllocation(int64_t key,
							   ClientChunk &chunk,
							   const std::vector<ChunkFaceInstanceGpu> &instances)
	{
		size_t cursor = 0;
		for (EntryIterator it = entriesList.begin(); it != entriesList.end(); ++it)
		{
			if (it->faceOffset >= cursor + instances.size())
			{
				ArenaEntry entry;
				entry.faceOffset = cursor;
				entry.reservedFaceCount = instances.size();
				entry.faceCount = chunk.renderState.faceCount;
				entry.chunk = &chunk;
				EntryIterator inserted = entriesList.insert(it, entry);
				entriesMap[key] = inserted;
				writeInstances(entry.faceOffset, instances);
				recomputeArenaReservedFaces();
				markArenaDirty();
				return;
			}
			cursor = it->faceOffset + it->reservedFaceCount;
		}

		ArenaEntry entry;
		entry.faceOffset = cursor;
		entry.reservedFaceCount = instances.size();
		entry.faceCount = chunk.renderState.faceCount;
		entry.chunk = &chunk;
		entriesList.push_back(entry);
		EntryIterator inserted = entriesList.end();
		--inserted;
		entriesMap[key] = inserted;
		writeInstances(entry.faceOffset, instances);
		recomputeArenaReservedFaces();
		markArenaDirty();
	}
};

#endif
