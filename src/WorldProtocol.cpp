#include <WorldProtocol.h>

#include <cstring>

namespace
{
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
	std::vector<uint8_t> buffer;
	buffer.reserve(1 + sizeof(int32_t) * 2 + sizeof(uint64_t) + sizeof(chunk.blocks));
	appendValue(buffer, PacketType::ChunkSnapshot);
	appendValue(buffer, chunk.chunkX);
	appendValue(buffer, chunk.chunkZ);
	appendValue(buffer, chunk.revision);
	size_t start = buffer.size();
	buffer.resize(start + sizeof(chunk.blocks));
	std::memcpy(buffer.data() + start, chunk.blocks, sizeof(chunk.blocks));
	return buffer;
}

bool decodeChunkSnapshot(const uint8_t *data, size_t size, DecodedChunkSnapshot &message)
{
	size_t offset = 0;
	if (!readPacketType(data, size, PacketType::ChunkSnapshot, offset))
	{
		return false;
	}
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
	}
	return "Unknown";
}
