#include <WorldProtocol.h>

#include <cstring>
#include <limits>

namespace
{
	struct ChunkSnapshotRleRun
	{
		uint16_t count = 0;
		uint32_t value = 0;
	};

	template <typename T>
	void appendValue(std::vector<uint8_t> &buffer, const T &value)
	{
		size_t start = buffer.size();
		buffer.resize(start + sizeof(T));
		std::memcpy(buffer.data() + start, &value, sizeof(T));
	}

	template <typename T>
	bool readValue(const uint8_t *data, size_t size, size_t &offset, T &value)
	{
		if (offset + sizeof(T) > size)
		{
			return false;
		}
		std::memcpy(&value, data + offset, sizeof(T));
		offset += sizeof(T);
		return true;
	}

	template <typename T>
	std::vector<uint8_t> encodeWithType(PacketType type, const T &value)
	{
		std::vector<uint8_t> buffer;
		buffer.reserve(1 + sizeof(T));
		appendValue(buffer, type);
		appendValue(buffer, value);
		return buffer;
	}

	bool readPacketType(const uint8_t *data, size_t size, PacketType expectedType, size_t &offset)
	{
		PacketType actualType = PacketType::Hello;
		if (!readValue(data, size, offset, actualType))
		{
			return false;
		}
		if (actualType != expectedType)
		{
			return false;
		}
		return true;
	}
}

std::vector<uint8_t> encodeHello(const HelloMessage &message)
{
	return encodeWithType(PacketType::Hello, message);
}

bool decodeHello(const uint8_t *data, size_t size, HelloMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::Hello, offset))
	{
		return false;
	}
	return readValue(data, size, offset, message);
}

std::vector<uint8_t> encodeWorldFrontier(const WorldFrontier &frontier)
{
	return encodeWithType(PacketType::WorldFrontier, frontier);
}

bool decodeWorldFrontier(const uint8_t *data, size_t size, WorldFrontier &frontier)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::WorldFrontier, offset))
	{
		return false;
	}
	return readValue(data, size, offset, frontier);
}

std::vector<uint8_t> encodeChunkRequest(const ChunkRequestMessage &message)
{
	return encodeWithType(PacketType::ChunkRequest, message);
}

bool decodeChunkRequest(const uint8_t *data, size_t size, ChunkRequestMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::ChunkRequest, offset))
	{
		return false;
	}
	return readValue(data, size, offset, message);
}

std::vector<uint8_t> encodeChunkDrop(const ChunkDropMessage &message)
{
	return encodeWithType(PacketType::ChunkDrop, message);
}

bool decodeChunkDrop(const uint8_t *data, size_t size, ChunkDropMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::ChunkDrop, offset))
	{
		return false;
	}
	return readValue(data, size, offset, message);
}

std::vector<uint8_t> encodeChunkSnapshot(const VoxelChunkData &chunk)
{
	std::vector<uint8_t> rawBuffer;
	rawBuffer.reserve(1 + sizeof(int32_t) * 2 + sizeof(uint64_t) + sizeof(chunk.blocks));
	appendValue(rawBuffer, PacketType::ChunkSnapshot);
	appendValue(rawBuffer, chunk.chunkX);
	appendValue(rawBuffer, chunk.chunkZ);
	appendValue(rawBuffer, chunk.revision);
	size_t rawStart = rawBuffer.size();
	rawBuffer.resize(rawStart + sizeof(chunk.blocks));
	std::memcpy(rawBuffer.data() + rawStart, chunk.blocks, sizeof(chunk.blocks));

	const uint32_t *values = &chunk.blocks[0][0][0];
	std::vector<ChunkSnapshotRleRun> runs;
	runs.reserve(CHUNK_BLOCK_COUNT);

	size_t index = 0;
	while (index < CHUNK_BLOCK_COUNT)
	{
		uint32_t value = values[index];
		uint16_t runCount = 1;
		while (index + static_cast<size_t>(runCount) < CHUNK_BLOCK_COUNT &&
			   values[index + static_cast<size_t>(runCount)] == value &&
			   runCount < std::numeric_limits<uint16_t>::max())
		{
			runCount++;
		}

		ChunkSnapshotRleRun run;
		run.count = runCount;
		run.value = value;
		runs.push_back(run);
		index += static_cast<size_t>(runCount);
	}

	std::vector<uint8_t> compressedBuffer;
	compressedBuffer.reserve(
		1 + sizeof(int32_t) * 2 + sizeof(uint64_t) + sizeof(uint32_t) +
		runs.size() * sizeof(ChunkSnapshotRleRun));
	appendValue(compressedBuffer, PacketType::ChunkSnapshotRle);
	appendValue(compressedBuffer, chunk.chunkX);
	appendValue(compressedBuffer, chunk.chunkZ);
	appendValue(compressedBuffer, chunk.revision);
	uint32_t runCount = static_cast<uint32_t>(runs.size());
	appendValue(compressedBuffer, runCount);
	for (const ChunkSnapshotRleRun &run : runs)
	{
		appendValue(compressedBuffer, run);
	}

	if (compressedBuffer.size() < rawBuffer.size())
	{
		return compressedBuffer;
	}
	return rawBuffer;
}

bool decodeChunkSnapshot(const uint8_t *data, size_t size, DecodedChunkSnapshot &message)
{
	size_t offset = 0;
	PacketType packetType = PacketType::Hello;
	if (!readValue(data, size, offset, packetType))
	{
		return false;
	}
	if (packetType == PacketType::ChunkSnapshot)
	{
		if (!readValue(data, size, offset, message.chunk.chunkX))
		{
			return false;
		}
		if (!readValue(data, size, offset, message.chunk.chunkZ))
		{
			return false;
		}
		if (!readValue(data, size, offset, message.chunk.revision))
		{
			return false;
		}
		if (offset + sizeof(message.chunk.blocks) > size)
		{
			return false;
		}
		std::memcpy(message.chunk.blocks, data + offset, sizeof(message.chunk.blocks));
		return true;
	}

	if (packetType == PacketType::ChunkSnapshotRle)
	{
		if (!readValue(data, size, offset, message.chunk.chunkX))
		{
			return false;
		}
		if (!readValue(data, size, offset, message.chunk.chunkZ))
		{
			return false;
		}
		if (!readValue(data, size, offset, message.chunk.revision))
		{
			return false;
		}

		uint32_t runCount = 0;
		if (!readValue(data, size, offset, runCount))
		{
			return false;
		}

		uint32_t *values = &message.chunk.blocks[0][0][0];
		size_t written = 0;
		for (uint32_t index = 0; index < runCount; index++)
		{
			ChunkSnapshotRleRun run;
			if (!readValue(data, size, offset, run))
			{
				return false;
			}
			if (written + static_cast<size_t>(run.count) > CHUNK_BLOCK_COUNT)
			{
				return false;
			}
			for (uint16_t countIndex = 0; countIndex < run.count; countIndex++)
			{
				values[written] = run.value;
				written++;
			}
		}
		if (written != CHUNK_BLOCK_COUNT)
		{
			return false;
		}
		return true;
	}

	return false;
}

std::vector<uint8_t> encodeBlockActionRequest(const BlockActionRequestMessage &message)
{
	return encodeWithType(PacketType::BlockActionRequest, message);
}

bool decodeBlockActionRequest(const uint8_t *data, size_t size, BlockActionRequestMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::BlockActionRequest, offset))
	{
		return false;
	}
	return readValue(data, size, offset, message);
}

std::vector<uint8_t> encodeBlockUpdateBroadcast(const BlockUpdateBroadcastMessage &message)
{
	return encodeWithType(PacketType::BlockUpdateBroadcast, message);
}

bool decodeBlockUpdateBroadcast(const uint8_t *data, size_t size, BlockUpdateBroadcastMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::BlockUpdateBroadcast, offset))
	{
		return false;
	}
	return readValue(data, size, offset, message);
}

const char *packetTypeName(PacketType type)
{
	switch (type)
	{
	case PacketType::Hello:
		return "Hello";
	case PacketType::WorldFrontier:
		return "WorldFrontier";
	case PacketType::ChunkRequest:
		return "ChunkRequest";
	case PacketType::ChunkSnapshot:
		return "ChunkSnapshot";
	case PacketType::ChunkDrop:
		return "ChunkDrop";
	case PacketType::BlockActionRequest:
		return "BlockActionRequest";
	case PacketType::BlockUpdateBroadcast:
		return "BlockUpdateBroadcast";
	case PacketType::ChunkSnapshotRle:
		return "ChunkSnapshotRle";
	}
	return "Unknown";
}
