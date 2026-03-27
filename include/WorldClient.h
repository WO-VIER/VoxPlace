#ifndef WORLD_CLIENT_H
#define WORLD_CLIENT_H

#include <PlayerData.h>
#include <WorldProtocol.h>
#include <glm/vec3.hpp>
#include <deque>
#include <string>

struct WorldClientEvent
{
	enum class Type
	{
		Connected,
		Disconnected,
		FrontierUpdated,
		ChunkReceived,
		BlockUpdated
	};

	Type type = Type::Disconnected;
	WorldFrontier frontier;
	VoxelChunkData chunk;
	BlockUpdateBroadcastMessage blockUpdate;
};

class WorldClient
{
public:
	WorldClient();
	~WorldClient();

	bool connectToServer(const std::string &hostName,
						 uint16_t port,
						 const std::string &username,
						 const std::string &password);
	void disconnect();
	void service();
	bool popEvent(WorldClientEvent &event);
	bool isConnected() const;
	const PlayerData &localPlayer() const;
	uint64_t remainingBlockActionCooldownMs() const;
	const std::string &lastConnectionError() const;

	void sendChunkRequest(int chunkX, int chunkZ);
	void sendChunkDrop(int chunkX, int chunkZ);
	void sendPlaceBlock(int worldX, int worldY, int worldZ, uint8_t paletteIndex);
	void sendBreakBlock(int worldX, int worldY, int worldZ);
	void sendPlayerMoveUpdate(const glm::vec3 &position, const glm::vec3 &lookDirection);
	void updateLocalPlayerTransform(const glm::vec3 &position, const glm::vec3 &lookDirection);

private:
	struct Impl;
	Impl *m_impl;
	std::deque<WorldClientEvent> m_events;

	void pushEvent(const WorldClientEvent &event);
	void handlePacket(const uint8_t *data, size_t size);
};

#endif
