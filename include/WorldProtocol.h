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

constexpr size_t PLAYER_PASSWORD_MAX_LENGTH = 128;

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
	PlayerState = 12,
	PlayerMoveUpdate = 13,
	ChunkSnapshotSectionsZstd = 14,
	ServerProfile = 15
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
	UsernameAlreadyInUse = 3,
	InvalidCredentials = 4
};

struct HelloMessage
{
	uint32_t magic = 0x5658504Cu;
	uint16_t version = 1;
};

struct LoginRequestMessage
{
	char username[PLAYER_USERNAME_MAX_LENGTH + 1] = {};
	char password[PLAYER_PASSWORD_MAX_LENGTH + 1] = {};
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

struct PlayerMoveUpdateMessage
{
	float positionX = 0.0f;
	float positionY = 35.0f;
	float positionZ = 0.0f;
	float lookX = 0.0f;
	float lookY = 0.0f;
	float lookZ = -1.0f;
};

struct ServerProfileMessage
{
	WorldGenerationMode mode = WorldGenerationMode::ActivityFrontier;
	uint16_t workerCount = 0;
	uint16_t clientCount = 0;
	uint32_t worldChunkCount = 0;
	uint32_t readyWindow = 0;
	uint32_t loadedWindow = 0;
	uint32_t generatedFreshWindow = 0;
	uint32_t loadErrorsWindow = 0;
	uint32_t integratedWindow = 0;
	uint32_t integratedLoadedWindow = 0;
	uint32_t integratedGeneratedWindow = 0;
	uint32_t unloadedWindow = 0;
	uint32_t queuedForSendWindow = 0;
	uint32_t sendQueueNow = 0;
	uint32_t dirtyMarkedWindow = 0;
	uint32_t dirtyQueueNow = 0;
	uint32_t saveQueueJobsNow = 0;
	uint32_t tasksNow = 0;
	float tasksAvg = 0.0f;
	uint32_t tasksMax = 0;
	uint32_t readyNow = 0;
	float readyAvg = 0.0f;
	uint32_t readyMax = 0;
	uint32_t snapshotCount = 0;
	float snapshotAvgBytes = 0.0f;
	float snapshotAvgRawBytes = 0.0f;
	float snapshotAvgSections = 0.0f;
	float snapshotRatio = 1.0f;
	float sqliteLoadChunkMsTotal = 0.0f;
	float sqliteLoadChunkMsAvg = 0.0f;
	float sqliteLoadChunkMsMax = 0.0f;
	float terrainGenChunkMsTotal = 0.0f;
	float terrainGenChunkMsAvg = 0.0f;
	float terrainGenChunkMsMax = 0.0f;
	uint32_t savedChunksWindow = 0;
	uint32_t saveBatchesWindow = 0;
	float saveAvgChunks = 0.0f;
	float ticksPerSecond = 0.0f;
	float windowSeconds = 0.0f;
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
std::vector<uint8_t> encodeChunkSnapshotNetwork(const VoxelChunkData &chunk);
bool decodeChunkSnapshot(const uint8_t *data, size_t size, DecodedChunkSnapshot &message);

std::vector<uint8_t> encodeBlockActionRequest(const BlockActionRequestMessage &message);
bool decodeBlockActionRequest(const uint8_t *data, size_t size, BlockActionRequestMessage &message);

std::vector<uint8_t> encodeBlockUpdateBroadcast(const BlockUpdateBroadcastMessage &message);
bool decodeBlockUpdateBroadcast(const uint8_t *data, size_t size, BlockUpdateBroadcastMessage &message);

std::vector<uint8_t> encodePlayerState(const PlayerStateMessage &message);
bool decodePlayerState(const uint8_t *data, size_t size, PlayerStateMessage &message);

std::vector<uint8_t> encodePlayerMoveUpdate(const PlayerMoveUpdateMessage &message);
bool decodePlayerMoveUpdate(const uint8_t *data, size_t size, PlayerMoveUpdateMessage &message);

std::vector<uint8_t> encodeServerProfile(const ServerProfileMessage &message);
bool decodeServerProfile(const uint8_t *data, size_t size, ServerProfileMessage &message);

const char *packetTypeName(PacketType type);

#endif
