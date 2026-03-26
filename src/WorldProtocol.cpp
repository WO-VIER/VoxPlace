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

	constexpr uint8_t VALID_CHUNK_SECTION_MASK =
		static_cast<uint8_t>((1u << CHUNK_SECTION_COUNT) - 1u);

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

	void appendChunkSectionData(std::vector<uint8_t> &buffer,
								const VoxelChunkData &chunk,
								int sectionIndex)
	{
		int yBegin = VoxelChunkData::sectionYBegin(sectionIndex);
		size_t xSliceBytes =
			static_cast<size_t>(CHUNK_SECTION_HEIGHT) *
			static_cast<size_t>(CHUNK_SIZE_Z) *
			sizeof(uint32_t);

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			size_t start = buffer.size();
			buffer.resize(start + xSliceBytes);
			std::memcpy(buffer.data() + start,
						&chunk.blocks[x][yBegin][0],
						xSliceBytes);
		}
	}

	bool readChunkSectionData(const uint8_t *data,
							  size_t size,
							  size_t &offset,
							  VoxelChunkData &chunk,
							  int sectionIndex)
	{
		int yBegin = VoxelChunkData::sectionYBegin(sectionIndex);
		size_t xSliceBytes =
			static_cast<size_t>(CHUNK_SECTION_HEIGHT) *
			static_cast<size_t>(CHUNK_SIZE_Z) *
			sizeof(uint32_t);

		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			if (offset + xSliceBytes > size)
			{
				return false;
			}
			std::memcpy(&chunk.blocks[x][yBegin][0], data + offset, xSliceBytes);
			offset += xSliceBytes;
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

std::vector<uint8_t> encodeLoginRequest(const LoginRequestMessage &message)
{
	return encodeWithType(PacketType::LoginRequest, message);
}

bool decodeLoginRequest(const uint8_t *data, size_t size, LoginRequestMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::LoginRequest, offset))
	{
		return false;
	}
	return readValue(data, size, offset, message);
}

std::vector<uint8_t> encodeLoginResponse(const LoginResponseMessage &message)
{
	return encodeWithType(PacketType::LoginResponse, message);
}

bool decodeLoginResponse(const uint8_t *data, size_t size, LoginResponseMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::LoginResponse, offset))
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
	std::vector<uint8_t> buffer;
	size_t sectionBytes = chunk.nonEmptySectionCount() *
		CHUNK_SECTION_BLOCK_COUNT *
		sizeof(uint32_t);
	buffer.reserve(1 + sizeof(int32_t) * 2 + sizeof(uint64_t) + sizeof(uint8_t) + sectionBytes);
	appendValue(buffer, PacketType::ChunkSnapshotSections);
	appendValue(buffer, chunk.chunkX);
	appendValue(buffer, chunk.chunkZ);
	appendValue(buffer, chunk.revision);
	appendValue(buffer, chunk.sectionMask());

	for (int sectionIndex = 0; sectionIndex < CHUNK_SECTION_COUNT; sectionIndex++)
	{
		if (chunk.isSectionEmpty(sectionIndex))
		{
			continue;
		}
		appendChunkSectionData(buffer, chunk, sectionIndex);
	}

	return buffer;
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
		message.chunk.rebuildSectionMask();
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
			message.chunk.rebuildSectionMask();
			return true;
		}

	if (packetType == PacketType::ChunkSnapshotSections)
	{
		message.chunk.clearBlocks();
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

		uint8_t sectionMask = 0;
		if (!readValue(data, size, offset, sectionMask))
		{
			return false;
		}
		if ((sectionMask & static_cast<uint8_t>(~VALID_CHUNK_SECTION_MASK)) != 0)
		{
			return false;
		}

		for (int sectionIndex = 0; sectionIndex < CHUNK_SECTION_COUNT; sectionIndex++)
		{
			uint8_t sectionBit = static_cast<uint8_t>(1u << sectionIndex);
			if ((sectionMask & sectionBit) == 0)
			{
				continue;
			}
			if (!readChunkSectionData(data, size, offset, message.chunk, sectionIndex))
			{
				return false;
			}
		}

		if (offset != size)
		{
			return false;
		}

		message.chunk.rebuildSectionMask();
		if (message.chunk.sectionMask() != sectionMask)
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

std::vector<uint8_t> encodePlayerState(const PlayerStateMessage &message)
{
	return encodeWithType(PacketType::PlayerState, message);
}

bool decodePlayerState(const uint8_t *data, size_t size, PlayerStateMessage &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::PlayerState, offset))
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
	case PacketType::LoginRequest:
		return "LoginRequest";
	case PacketType::LoginResponse:
		return "LoginResponse";
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
	case PacketType::PlayerState:
		return "PlayerState";
	case PacketType::ChunkSnapshotRle:
		return "ChunkSnapshotRle";
	case PacketType::ChunkSnapshotSections:
		return "ChunkSnapshotSections";
	}
	return "Unknown";
}
