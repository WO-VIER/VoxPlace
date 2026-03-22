#include <WorldClient.h>

#include <enet/enet.h>

#include <iostream>
#include <utility>

namespace
{
	constexpr size_t WORLD_CHANNEL_RELIABLE = 0;
}

struct WorldClient::Impl
{
	bool enetInitialized = false;
	ENetHost *host = nullptr;
	ENetPeer *peer = nullptr;
	bool connected = false;
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

bool WorldClient::connectToServer(const std::string &hostName, uint16_t port)
{
	if (!m_impl->enetInitialized)
	{
		std::cerr << "ENet client initialization failed" << std::endl;
		return false;
	}

	if (m_impl->host == nullptr)
	{
		m_impl->host = enet_host_create(nullptr, 1, 2, 0, 0);
		if (m_impl->host == nullptr)
		{
			std::cerr << "Failed to create ENet client host" << std::endl;
			return false;
		}
	}

	ENetAddress address{};
	address.port = port;
	if (enet_address_set_host(&address, hostName.c_str()) != 0)
	{
		std::cerr << "Failed to resolve server host: " << hostName << std::endl;
		return false;
	}

	m_impl->peer = enet_host_connect(m_impl->host, &address, 2, 0);
	if (m_impl->peer == nullptr)
	{
		std::cerr << "Failed to create ENet peer" << std::endl;
		return false;
	}

	ENetEvent event{};
	if (enet_host_service(m_impl->host, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
	{
		m_impl->connected = true;
		pushEvent(WorldClientEvent{WorldClientEvent::Type::Connected});
		HelloMessage hello;
		std::vector<uint8_t> payload = encodeHello(hello);
		ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, packet);
		enet_host_flush(m_impl->host);
		return true;
	}

	enet_peer_reset(m_impl->peer);
	m_impl->peer = nullptr;
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

	BlockActionRequestMessage message;
	message.action = BlockActionType::Place;
	message.worldX = worldX;
	message.worldY = worldY;
	message.worldZ = worldZ;
	message.paletteIndex = paletteIndex;

	std::vector<uint8_t> payload = encodeBlockActionRequest(message);
	ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(m_impl->peer, WORLD_CHANNEL_RELIABLE, packet);
}

void WorldClient::sendBreakBlock(int worldX, int worldY, int worldZ)
{
	if (!m_impl->connected || m_impl->peer == nullptr)
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
