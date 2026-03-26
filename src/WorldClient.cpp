#include <WorldClient.h>

#include <PlayerHotData.h>
#include <PlayerUsername.h>

#include <enet/enet.h>

#include <chrono>
#include <iostream>
#include <utility>

namespace
{
	constexpr size_t WORLD_CHANNEL_RELIABLE = 0;

	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}
}

struct WorldClient::Impl
{
	bool enetInitialized = false;
	ENetHost *host = nullptr;
	ENetPeer *peer = nullptr;
	bool connected = false;
	PlayerData localPlayer;
	std::string lastConnectionError;
	int64_t serverTimeOffsetMs = 0;

	uint64_t estimatedServerNowMs() const
	{
		int64_t localNow = static_cast<int64_t>(systemNowMs());
		int64_t estimated = localNow + serverTimeOffsetMs;
		if (estimated < 0)
		{
			return 0;
		}
		return static_cast<uint64_t>(estimated);
	}

	bool canIssueBlockAction() const
	{
		return estimatedServerNowMs() >= localPlayer.hot.blockActionReadyAtMs;
	}

	void applyLoginResponse(const LoginResponseMessage &message)
	{
		localPlayer.cold.playerId = message.playerId;
		localPlayer.cold.username = playerUsernameFromBuffer(message.username);
		localPlayer.cold.skinId = message.skinId;
		localPlayer.hot.position = glm::vec3(
			message.positionX,
			message.positionY,
			message.positionZ);
		localPlayer.hot.blockActionReadyAtMs = message.blockActionReadyAtMs;

		int64_t localNow = static_cast<int64_t>(systemNowMs());
		int64_t serverNow = static_cast<int64_t>(message.serverNowMs);
		serverTimeOffsetMs = serverNow - localNow;
	}

	void applyPlayerState(const PlayerStateMessage &message)
	{
		if (localPlayer.cold.playerId != 0 && message.playerId != localPlayer.cold.playerId)
		{
			return;
		}

		localPlayer.hot.position = glm::vec3(
			message.positionX,
			message.positionY,
			message.positionZ);
		localPlayer.hot.blockActionReadyAtMs = message.blockActionReadyAtMs;

		int64_t localNow = static_cast<int64_t>(systemNowMs());
		int64_t serverNow = static_cast<int64_t>(message.serverNowMs);
		serverTimeOffsetMs = serverNow - localNow;
	}
};

WorldClient::WorldClient()
{
	m_impl = new Impl();
	if (enet_initialize() == 0)
	{
		m_impl->enetInitialized = true;
	}
}

WorldClient::~WorldClient()
{
	disconnect();
	if (m_impl->host != nullptr)
	{
		enet_host_destroy(m_impl->host);
		m_impl->host = nullptr;
	}
	if (m_impl->enetInitialized)
	{
		enet_deinitialize();
	}
	delete m_impl;
}

bool WorldClient::connectToServer(const std::string &hostName,
								  uint16_t port,
								  const std::string &username)
{
	m_impl->lastConnectionError.clear();

	if (!m_impl->enetInitialized)
	{
		m_impl->lastConnectionError = "ENet client initialization failed";
		return false;
	}

	std::string trimmedUsername = trimPlayerUsername(username);
	PlayerUsernameValidationError usernameError = validatePlayerUsername(trimmedUsername);
	if (usernameError != PlayerUsernameValidationError::None)
	{
		m_impl->lastConnectionError = playerUsernameValidationErrorText(usernameError);
		return false;
	}

	if (m_impl->host == nullptr)
	{
		m_impl->host = enet_host_create(nullptr, 1, 2, 0, 0);
		if (m_impl->host == nullptr)
		{
			m_impl->lastConnectionError = "Failed to create ENet client host";
			return false;
		}
	}

	ENetAddress address{};
	address.port = port;
	if (enet_address_set_host(&address, hostName.c_str()) != 0)
	{
		m_impl->lastConnectionError = "Failed to resolve server host: " + hostName;
		return false;
	}

	m_impl->peer = enet_host_connect(m_impl->host, &address, 2, 0);
	if (m_impl->peer == nullptr)
	{
		m_impl->lastConnectionError = "Failed to create ENet peer";
		return false;
	}

	ENetEvent event{};
	if (enet_host_service(m_impl->host, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
	{
		HelloMessage hello;
		std::vector<uint8_t> helloPayload = encodeHello(hello);
		ENetPacket *helloPacket = enet_packet_create(
			helloPayload.data(),
			helloPayload.size(),
			ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, helloPacket);

		LoginRequestMessage login;
		(void)copyPlayerUsernameToBuffer(trimmedUsername, login.username);
		std::vector<uint8_t> loginPayload = encodeLoginRequest(login);
		ENetPacket *loginPacket = enet_packet_create(
			loginPayload.data(),
			loginPayload.size(),
			ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, loginPacket);
		enet_host_flush(m_impl->host);

		auto start = std::chrono::steady_clock::now();
		while (true)
		{
			uint64_t elapsedMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - start)
					.count());
			if (elapsedMs >= 5000)
			{
				break;
			}

			int waitMs = static_cast<int>(5000 - elapsedMs);
			if (waitMs > 250)
			{
				waitMs = 250;
			}

			if (enet_host_service(m_impl->host, &event, waitMs) <= 0)
			{
				continue;
			}

			if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				if (event.packet->dataLength > 0)
				{
					PacketType packetType = static_cast<PacketType>(event.packet->data[0]);
					if (packetType == PacketType::LoginResponse)
					{
						LoginResponseMessage response;
						bool decoded = decodeLoginResponse(
							event.packet->data,
							event.packet->dataLength,
							response);
						enet_packet_destroy(event.packet);
						if (!decoded)
						{
							m_impl->lastConnectionError = "Failed to decode login response";
							break;
						}
						if (response.status != LoginStatus::Accepted)
						{
							if (response.status == LoginStatus::InvalidUsername)
							{
								m_impl->lastConnectionError = "Server rejected username";
							}
							else if (response.status == LoginStatus::UsernameAlreadyInUse)
							{
								m_impl->lastConnectionError = "Username already in use";
							}
							else
							{
								m_impl->lastConnectionError = "Login rejected";
							}
							break;
						}

						m_impl->applyLoginResponse(response);
						m_impl->connected = true;
						pushEvent(WorldClientEvent{WorldClientEvent::Type::Connected});
						return true;
					}

					handlePacket(event.packet->data, event.packet->dataLength);
				}
				enet_packet_destroy(event.packet);
				continue;
			}

			if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			{
				m_impl->lastConnectionError = "Disconnected during login";
				break;
			}
		}
	}
	else
	{
		m_impl->lastConnectionError = "Timed out while connecting to server";
	}

	enet_peer_reset(m_impl->peer);
	m_impl->peer = nullptr;
	m_impl->connected = false;
	return false;
}

void WorldClient::disconnect()
{
	if (m_impl->peer == nullptr)
	{
		m_impl->connected = false;
		return;
	}

	enet_peer_disconnect(m_impl->peer, 0);
	ENetEvent event{};
	while (enet_host_service(m_impl->host, &event, 100) > 0)
	{
		if (event.type == ENET_EVENT_TYPE_RECEIVE)
		{
			enet_packet_destroy(event.packet);
		}
		if (event.type == ENET_EVENT_TYPE_DISCONNECT)
		{
			break;
		}
	}
	enet_peer_reset(m_impl->peer);
	m_impl->peer = nullptr;
	m_impl->connected = false;
	m_impl->localPlayer = PlayerData{};
	m_impl->serverTimeOffsetMs = 0;
}

void WorldClient::service()
{
	if (m_impl->host == nullptr)
	{
		return;
	}

	ENetEvent event{};
	while (enet_host_service(m_impl->host, &event, 0) > 0)
	{
		if (event.type == ENET_EVENT_TYPE_RECEIVE)
		{
			handlePacket(event.packet->data, event.packet->dataLength);
			enet_packet_destroy(event.packet);
			continue;
		}
		if (event.type == ENET_EVENT_TYPE_DISCONNECT)
		{
			m_impl->peer = nullptr;
			m_impl->connected = false;
			pushEvent(WorldClientEvent{WorldClientEvent::Type::Disconnected});
		}
	}
}

bool WorldClient::popEvent(WorldClientEvent &event)
{
	if (m_events.empty())
	{
		return false;
	}
	event = std::move(m_events.front());
	m_events.pop_front();
	return true;
}

bool WorldClient::isConnected() const
{
	return m_impl->connected;
}

const PlayerData &WorldClient::localPlayer() const
{
	return m_impl->localPlayer;
}

const std::string &WorldClient::lastConnectionError() const
{
	return m_impl->lastConnectionError;
}

void WorldClient::sendChunkRequest(int chunkX, int chunkZ)
{
	if (!m_impl->connected || m_impl->peer == nullptr)
	{
		return;
	}

	ChunkRequestMessage message;
	message.chunkX = chunkX;
	message.chunkZ = chunkZ;
	std::vector<uint8_t> payload = encodeChunkRequest(message);
	ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, packet);
}

void WorldClient::sendChunkDrop(int chunkX, int chunkZ)
{
	if (!m_impl->connected || m_impl->peer == nullptr)
	{
		return;
	}

	ChunkDropMessage message;
	message.chunkX = chunkX;
	message.chunkZ = chunkZ;
	std::vector<uint8_t> payload = encodeChunkDrop(message);
	ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, packet);
}

void WorldClient::sendPlaceBlock(int worldX, int worldY, int worldZ, uint8_t paletteIndex)
{
	if (!m_impl->connected || m_impl->peer == nullptr)
	{
		return;
	}
	if (!m_impl->canIssueBlockAction())
	{
		return;
	}

	BlockActionRequestMessage message;
	message.action = BlockActionType::Place;
	message.worldX = worldX;
	message.worldY = worldY;
	message.worldZ = worldZ;
	message.paletteIndex = paletteIndex;

	std::vector<uint8_t> payload = encodeBlockActionRequest(message);
	ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, packet);
	m_impl->localPlayer.hot.blockActionReadyAtMs =
		m_impl->estimatedServerNowMs() + PLAYER_DEFAULT_BLOCK_ACTION_COOLDOWN_MS;
}

void WorldClient::sendBreakBlock(int worldX, int worldY, int worldZ)
{
	if (!m_impl->connected || m_impl->peer == nullptr)
	{
		return;
	}
	if (!m_impl->canIssueBlockAction())
	{
		return;
	}

	BlockActionRequestMessage message;
	message.action = BlockActionType::Break;
	message.worldX = worldX;
	message.worldY = worldY;
	message.worldZ = worldZ;
	message.paletteIndex = 0;

	std::vector<uint8_t> payload = encodeBlockActionRequest(message);
	ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, packet);
	m_impl->localPlayer.hot.blockActionReadyAtMs =
		m_impl->estimatedServerNowMs() + PLAYER_DEFAULT_BLOCK_ACTION_COOLDOWN_MS;
}

void WorldClient::pushEvent(const WorldClientEvent &event)
{
	m_events.push_back(event);
}

void WorldClient::handlePacket(const uint8_t *data, size_t size)
{
	if (size == 0)
	{
		return;
	}

	PacketType type = static_cast<PacketType>(data[0]);

	if (type == PacketType::Hello)
	{
		HelloMessage message;
		(void)decodeHello(data, size, message);
		return;
	}

	if (type == PacketType::PlayerState)
	{
		PlayerStateMessage message;
		if (!decodePlayerState(data, size, message))
		{
			return;
		}
		m_impl->applyPlayerState(message);
		return;
	}

	if (type == PacketType::WorldFrontier)
	{
		WorldFrontier frontier;
		if (!decodeWorldFrontier(data, size, frontier))
		{
			return;
		}
		WorldClientEvent event;
		event.type = WorldClientEvent::Type::FrontierUpdated;
		event.frontier = frontier;
		pushEvent(event);
		return;
	}

	if (type == PacketType::ChunkSnapshot ||
		type == PacketType::ChunkSnapshotRle ||
		type == PacketType::ChunkSnapshotSections)
	{
		DecodedChunkSnapshot snapshot;
		if (!decodeChunkSnapshot(data, size, snapshot))
		{
			return;
		}
		WorldClientEvent event;
		event.type = WorldClientEvent::Type::ChunkReceived;
		event.chunk = snapshot.chunk;
		pushEvent(event);
		return;
	}

	if (type == PacketType::BlockUpdateBroadcast)
	{
		BlockUpdateBroadcastMessage message;
		if (!decodeBlockUpdateBroadcast(data, size, message))
		{
			return;
		}
		WorldClientEvent event;
		event.type = WorldClientEvent::Type::BlockUpdated;
		event.blockUpdate = message;
		pushEvent(event);
	}
}
