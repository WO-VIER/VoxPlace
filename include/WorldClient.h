#ifndef WORLD_CLIENT_H
#define WORLD_CLIENT_H

#include <Player.h>
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
			PlayerStateUpdated,
			FrontierUpdated,
			ChunkReceived,
			BlockUpdated,
			ChatMessageReceived,
			ExpansionStatusUpdated,
			ServerProfileUpdated
		};

	Type type = Type::Disconnected;
	PlayerStateMessage playerState;
	WorldFrontier frontier;
		VoxelChunkData chunk;
		BlockUpdateBroadcastMessage blockUpdate;
		ExpansionStatusMessage expansionStatus;
		ServerChatMessage chatMessage;
		ServerProfileMessage serverProfile;
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
	bool deleteUserOnServer(const std::string &hostName,
							uint16_t port,
							const std::string &username,
							const std::string &password);
	void disconnect();
	void service();
	bool popEvent(WorldClientEvent &event);
	bool isConnected() const;
	uint32_t getRoundTripTime() const;
	const Player &localPlayer() const;
	uint64_t remainingBlockActionCooldownMs() const;
	uint64_t remainingServerCooldownMs(uint64_t readyAtMs) const;
	const std::string &lastConnectionError() const;

	void sendChunkRequest(int chunkX, int chunkZ);
	void sendChunkDrop(int chunkX, int chunkZ);
		void sendPlaceBlock(int worldX, int worldY, int worldZ, uint8_t paletteIndex);
		void sendBreakBlock(int worldX, int worldY, int worldZ);
		void sendCommand(const std::string &commandText);
		void sendChatMessage(const std::string &messageText);
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
