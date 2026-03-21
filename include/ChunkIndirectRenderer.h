#ifndef CHUNK_INDIRECT_RENDERER_H
#define CHUNK_INDIRECT_RENDERER_H

#include <Chunk2.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <limits>
#include <unordered_map>
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
	struct ArenaEntry
	{
		size_t wordOffset = 0;
		size_t reservedWordCount = 0;
		uint32_t faceCount = 0;
		Chunk2 *chunk = nullptr;
	};

	GLuint facesSsbo = 0;
	GLuint drawInfoSsbo = 0;
	GLuint indirectBuffer = 0;
	GLuint vao = 0;
	size_t drawCount = 0;
	size_t faceCount = 0;
	size_t arenaWordCapacity = 0;
	size_t arenaReservedWords = 0;
	uint64_t arenaVersion = 0;
	uint64_t cachedArenaVersion = std::numeric_limits<uint64_t>::max();
	uint64_t drawDataBuildCount = 0;
	bool lastBuildReused = false;

	void init(size_t initialWordCapacity = 0)
	{
		cleanup();

		if (initialWordCapacity == 0)
		{
			initialWordCapacity = 4 * 1024 * 1024;
		}
		arenaWordCapacity = initialWordCapacity;

		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &facesSsbo);
		glGenBuffers(1, &drawInfoSsbo);
		glGenBuffers(1, &indirectBuffer);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, facesSsbo);
		glBufferData(
			GL_SHADER_STORAGE_BUFFER,
			arenaWordCapacity * sizeof(uint32_t),
			nullptr,
			GL_DYNAMIC_DRAW);
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
		arenaWordCapacity = 0;
		arenaReservedWords = 0;
		arenaVersion = 0;
		cachedArenaVersion = std::numeric_limits<uint64_t>::max();
		drawDataBuildCount = 0;
		lastBuildReused = false;
		entriesList.clear();
		entriesMap.clear();
		cachedVisibleKeys.clear();
	}

	void upsertChunk(Chunk2 &chunk)
	{
		const std::vector<uint32_t> &packedFaces = chunk.packedFaces();
		int64_t key = chunkKey(chunk.chunkX, chunk.chunkZ);

		if (packedFaces.empty() || chunk.faceCount == 0)
		{
			removeChunk(key);
			return;
		}

		auto entryIt = entriesMap.find(key);
		if (entryIt != entriesMap.end())
		{
			ArenaEntry &entry = *entryIt->second;
			entry.chunk = &chunk;

			if (packedFaces.size() <= entry.reservedWordCount)
			{
				entry.faceCount = chunk.faceCount;
				writeWords(entry.wordOffset, packedFaces);
				recomputeArenaReservedWords();
				markArenaDirty();
				return;
			}

			entriesList.erase(entryIt->second);
			entriesMap.erase(entryIt);
		}

		ensureSpaceFor(packedFaces.size());
		insertChunkAllocation(key, chunk, packedFaces);
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
		recomputeArenaReservedWords();
		markArenaDirty();
	}

	void buildVisibleDrawData(const std::vector<Chunk2 *> &visibleChunks)
	{
		std::vector<int64_t> visibleKeys;
		std::vector<ChunkIndirectDrawInfoGpu> drawInfos;
		std::vector<ChunkDrawArraysIndirectCommand> drawCommands;

		visibleKeys.reserve(visibleChunks.size());
		drawInfos.reserve(visibleChunks.size());
		drawCommands.reserve(visibleChunks.size());

		faceCount = 0;
		for (Chunk2 *chunk : visibleChunks)
		{
			if (chunk == nullptr)
			{
				continue;
			}

			int64_t key = chunkKey(chunk->chunkX, chunk->chunkZ);
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

			ChunkIndirectDrawInfoGpu drawInfo;
			drawInfo.faceOffset = static_cast<uint32_t>(entry.wordOffset / 2);
			drawInfo.chunkX = chunk->chunkX;
			drawInfo.chunkZ = chunk->chunkZ;
			drawInfos.push_back(drawInfo);

			ChunkDrawArraysIndirectCommand command;
			command.count = entry.faceCount * 6;
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

	void recomputeArenaReservedWords()
	{
		size_t reservedWords = 0;
		for (const ArenaEntry &entry : entriesList)
		{
			reservedWords += entry.reservedWordCount;
		}
		arenaReservedWords = reservedWords;
	}

	void writeWords(size_t wordOffset, const std::vector<uint32_t> &words)
	{
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, facesSsbo);
		glBufferSubData(
			GL_SHADER_STORAGE_BUFFER,
			static_cast<GLintptr>(wordOffset * sizeof(uint32_t)),
			static_cast<GLsizeiptr>(words.size() * sizeof(uint32_t)),
			words.data());
	}

	void ensureSpaceFor(size_t requiredWords)
	{
		while (!hasGapFor(requiredWords))
		{
			growArena(requiredWords);
		}
	}

	bool hasGapFor(size_t requiredWords) const
	{
		size_t cursor = 0;
		for (const ArenaEntry &entry : entriesList)
		{
			if (entry.wordOffset >= cursor + requiredWords)
			{
				return true;
			}
			cursor = entry.wordOffset + entry.reservedWordCount;
		}
		if (arenaWordCapacity >= cursor + requiredWords)
		{
			return true;
		}
		return false;
	}

	void growArena(size_t requiredWords)
	{
		size_t newCapacity = arenaWordCapacity;
		if (newCapacity == 0)
		{
			newCapacity = requiredWords;
		}
		while (newCapacity < arenaReservedWords + requiredWords)
		{
			newCapacity *= 2;
			if (newCapacity == 0)
			{
				newCapacity = requiredWords;
			}
		}

		GLuint newFacesSsbo = 0;
		glGenBuffers(1, &newFacesSsbo);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, newFacesSsbo);
		glBufferData(
			GL_SHADER_STORAGE_BUFFER,
			newCapacity * sizeof(uint32_t),
			nullptr,
			GL_DYNAMIC_DRAW);

		size_t cursor = 0;
		for (ArenaEntry &entry : entriesList)
		{
			if (entry.chunk == nullptr)
			{
				continue;
			}
			const std::vector<uint32_t> &words = entry.chunk->packedFaces();
			entry.wordOffset = cursor;
			entry.reservedWordCount = words.size();
			entry.faceCount = entry.chunk->faceCount;

			glBufferSubData(
				GL_SHADER_STORAGE_BUFFER,
				static_cast<GLintptr>(entry.wordOffset * sizeof(uint32_t)),
				static_cast<GLsizeiptr>(words.size() * sizeof(uint32_t)),
				words.data());
			cursor += entry.reservedWordCount;
		}

		if (facesSsbo != 0)
		{
			glDeleteBuffers(1, &facesSsbo);
		}
		facesSsbo = newFacesSsbo;
		arenaWordCapacity = newCapacity;
		recomputeArenaReservedWords();
		markArenaDirty();
	}

	void insertChunkAllocation(int64_t key, Chunk2 &chunk, const std::vector<uint32_t> &packedFaces)
	{
		size_t cursor = 0;
		for (EntryIterator it = entriesList.begin(); it != entriesList.end(); ++it)
		{
			if (it->wordOffset >= cursor + packedFaces.size())
			{
				ArenaEntry entry;
				entry.wordOffset = cursor;
				entry.reservedWordCount = packedFaces.size();
				entry.faceCount = chunk.faceCount;
				entry.chunk = &chunk;
				EntryIterator inserted = entriesList.insert(it, entry);
				entriesMap[key] = inserted;
				writeWords(entry.wordOffset, packedFaces);
				recomputeArenaReservedWords();
				return;
			}
			cursor = it->wordOffset + it->reservedWordCount;
		}

		ArenaEntry entry;
		entry.wordOffset = cursor;
		entry.reservedWordCount = packedFaces.size();
		entry.faceCount = chunk.faceCount;
		entry.chunk = &chunk;
		entriesList.push_back(entry);
		EntryIterator inserted = entriesList.end();
		--inserted;
		entriesMap[key] = inserted;
		writeWords(entry.wordOffset, packedFaces);
		recomputeArenaReservedWords();
		markArenaDirty();
	}
};

#endif
