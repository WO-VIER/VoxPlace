#include <client/gameplay/ClientWorldSystem.h>

#include <client/gameplay/MeshBuildSystem.h>
#include <client/gameplay/PlayerSyncSystem.h>

#include <chrono>

#include <glm/geometric.hpp>

namespace
{
	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}

	void pushChatMessage(ClientWorldState &worldState, const ClientChatMessage &message)
	{
		if (message.text.empty())
		{
			return;
		}
		worldState.chatMessages.push_back(message);
		while (worldState.chatMessages.size() > 64)
		{
			worldState.chatMessages.pop_front();
		}
	}

	std::string packetText(const char *buffer, size_t size)
	{
		size_t length = 0;
		while (length < size && buffer[length] != '\0')
		{
			length++;
		}
		return std::string(buffer, length);
	}
}

void ClientWorldSystem::clear(ClientWorldState &worldState, ChunkIndirectRenderer &indirectRenderer)
{
	worldState.hasWorldFrontier = false;
	worldState.hasExpansionStatus = false;
	worldState.hasServerProfile = false;
	worldState.hasPreviousCameraPosition = false;
	worldState.previousCameraPosition = glm::vec3(0.0f);
	worldState.frontier = WorldFrontier{};
	worldState.expansionStatus = ExpansionStatusMessage{};
	worldState.serverProfile = ServerProfileMessage{};
	worldState.chatMessages.clear();
	worldState.pendingMeshRevisions.clear();
	worldState.profileChunkRequestsWindow = 0;
	worldState.profileChunkDropsWindow = 0;
	worldState.profileChunkReceivesWindow = 0;
	worldState.profileChunkUnloadsWindow = 0;
	worldState.profileMeshedChunkCountWindow = 0;
	worldState.profileMeshedSectionCountWindow = 0;

	for (auto &[key, chunk] : worldState.chunkMap)
	{
		delete chunk;
	}
	worldState.chunkMap.clear();
	worldState.streamedChunkKeys.clear();

	indirectRenderer.cleanup();
	indirectRenderer.init();
}

void ClientWorldSystem::handleEvents(WorldClient &worldClient,
									 ClientWorldState &worldState,
									 ChunkIndirectRenderer &indirectRenderer,
									 LoginScreen &loginScreen,
									 GLFWwindow *window,
									 GameState &gameState,
									 Camera &camera)
{
	worldClient.service();

	WorldClientEvent event;
	while (worldClient.popEvent(event))
	{
		if (event.type == WorldClientEvent::Type::Disconnected)
		{
			loginScreen.handleDisconnected(
				[&]()
				{
					clear(worldState, indirectRenderer);
				},
				window,
				"Disconnected from server");
			resetPlayerMovementSyncState(gameState, camera, worldClient);
			gameState.command = ClientCommandState{};
			gameState.input.commandToggleHeld = false;
			gameState.input.commandCancelHeld = false;
			gameState.input.suppressEscapeRelease = false;
			gameState.appState = ClientAppState::Login;
			continue;
		}
		if (event.type == WorldClientEvent::Type::PlayerStateUpdated)
		{
			glm::vec3 playerPosition = worldClient.localPlayer().state.position;
			glm::vec3 delta = camera.Position - playerPosition;
			if (glm::dot(delta, delta) > 1.0f)
			{
				camera.Position = playerPosition;
				resetPlayerMovementSyncState(gameState, camera, worldClient);
			}
			continue;
		}
		if (event.type == WorldClientEvent::Type::FrontierUpdated)
		{
			worldState.frontier = event.frontier;
			worldState.hasWorldFrontier = true;
			continue;
		}
		if (event.type == WorldClientEvent::Type::ChunkReceived)
		{
			MeshBuildSystem::upsertChunkSnapshot(worldState.chunkMap, worldState.streamedChunkKeys, event.chunk);
			worldState.profileChunkReceivesWindow++;
			continue;
		}
		if (event.type == WorldClientEvent::Type::BlockUpdated)
		{
			MeshBuildSystem::applyBlockUpdateLocal(
				worldState.chunkMap,
				event.blockUpdate.worldX,
				event.blockUpdate.worldY,
				event.blockUpdate.worldZ,
				event.blockUpdate.finalColor);
			continue;
		}
		if (event.type == WorldClientEvent::Type::ChatMessageReceived)
		{
			ClientChatMessage message;
			message.kind = event.chatMessage.kind;
			message.username = packetText(event.chatMessage.username, sizeof(event.chatMessage.username));
			message.text = packetText(event.chatMessage.text, sizeof(event.chatMessage.text));
			message.receivedAtMs = systemNowMs();
			pushChatMessage(worldState, message);
			continue;
		}
		if (event.type == WorldClientEvent::Type::ExpansionStatusUpdated)
		{
			worldState.expansionStatus = event.expansionStatus;
			worldState.hasExpansionStatus = true;
			continue;
		}
		if (event.type == WorldClientEvent::Type::ServerProfileUpdated)
		{
			worldState.serverProfile = event.serverProfile;
			worldState.hasServerProfile = true;
		}
	}
}
