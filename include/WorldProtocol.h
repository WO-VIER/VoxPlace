#ifndef WORLD_PROTOCOL_H
#define WORLD_PROTOCOL_H

#include <PlayerUsername.h>
#include <VoxelChunkData.h>
#include <WorldBounds.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

enum class PacketType : uint8_t
{
	Hello = 1,
	WorldFrontier = 2,
	ChunkRequest = 3,
	ChunkSnapshot = 4,
	ChunkDrop = 5,
	BlockActionRequest = 6,
	BlockUpdateBroadcast = 7,
	ChunkSnapshotRle = 8,
	ChunkSnapshotSections = 9,
	LoginRequest = 10,
	LoginResponse = 11,
	PlayerState = 12
};

enum class BlockActionType : uint8_t
{
	Place = 1,
	Break = 2
};

enum class LoginStatus : uint8_t
{
	Accepted = 1,
	InvalidUsername = 2,
	UsernameAlreadyInUse = 3
};

struct HelloMessage
{
	uint32_t magic = 0x5658504Cu;
	uint16_t version = 1;
};

struct LoginRequestMessage
{
	char username[PLAYER_USERNAME_MAX_LENGTH + 1] = {};
};

struct LoginResponseMessage
{
	LoginStatus status = LoginStatus::InvalidUsername;
	uint64_t playerId = 0;
	char username[PLAYER_USERNAME_MAX_LENGTH + 1] = {};
	uint16_t skinId = 0;
	float positionX = 0.0f;
	float positionY = 35.0f;
	float positionZ = 0.0f;
	uint64_t blockActionReadyAtMs = 0;
	uint64_t serverNowMs = 0;
};

struct ChunkRequestMessage
{
	int32_t chunkX = 0;
	int32_t chunkZ = 0;
};

struct ChunkDropMessage
{
	int32_t chunkX = 0;
	int32_t chunkZ = 0;
};

struct BlockActionRequestMessage
{
	BlockActionType action = BlockActionType::Place;
	int32_t worldX = 0;
	int32_t worldY = 0;
	int32_t worldZ = 0;
	uint8_t paletteIndex = 0;
};

struct BlockUpdateBroadcastMessage
{
	int32_t worldX = 0;
	int32_t worldY = 0;
	int32_t worldZ = 0;
	uint32_t finalColor = 0;
	uint64_t revision = 0;
};

struct PlayerStateMessage
{
	uint64_t playerId = 0;
	float positionX = 0.0f;
	float positionY = 35.0f;
	float positionZ = 0.0f;
	uint64_t blockActionReadyAtMs = 0;
	uint64_t serverNowMs = 0;
};

struct DecodedChunkSnapshot
{
	VoxelChunkData chunk;
};

std::vector<uint8_t> encodeHello(const HelloMessage &message);
bool decodeHello(const uint8_t *data, size_t size, HelloMessage &message);

std::vector<uint8_t> encodeLoginRequest(const LoginRequestMessage &message);
bool decodeLoginRequest(const uint8_t *data, size_t size, LoginRequestMessage &message);

std::vector<uint8_t> encodeLoginResponse(const LoginResponseMessage &message);
bool decodeLoginResponse(const uint8_t *data, size_t size, LoginResponseMessage &message);

std::vector<uint8_t> encodeWorldFrontier(const WorldFrontier &frontier);
bool decodeWorldFrontier(const uint8_t *data, size_t size, WorldFrontier &frontier);

std::vector<uint8_t> encodeChunkRequest(const ChunkRequestMessage &message);
bool decodeChunkRequest(const uint8_t *data, size_t size, ChunkRequestMessage &message);

std::vector<uint8_t> encodeChunkDrop(const ChunkDropMessage &message);
bool decodeChunkDrop(const uint8_t *data, size_t size, ChunkDropMessage &message);

std::vector<uint8_t> encodeChunkSnapshot(const VoxelChunkData &chunk);
bool decodeChunkSnapshot(const uint8_t *data, size_t size, DecodedChunkSnapshot &message);

std::vector<uint8_t> encodeBlockActionRequest(const BlockActionRequestMessage &message);
bool decodeBlockActionRequest(const uint8_t *data, size_t size, BlockActionRequestMessage &message);

std::vector<uint8_t> encodeBlockUpdateBroadcast(const BlockUpdateBroadcastMessage &message);
bool decodeBlockUpdateBroadcast(const uint8_t *data, size_t size, BlockUpdateBroadcastMessage &message);

std::vector<uint8_t> encodePlayerState(const PlayerStateMessage &message);
bool decodePlayerState(const uint8_t *data, size_t size, PlayerStateMessage &message);

const char *packetTypeName(PacketType type);

#endif
