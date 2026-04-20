#include <client/gameplay/ClientWorldSystem.h>

#include <client/gameplay/MeshBuildSystem.h>
#include <client/gameplay/PlayerSyncSystem.h>

#include <glm/geometric.hpp>

namespace
{
	void pushServerMessage(ClientWorldState &worldState, const std::string &message)
	{
		if (message.empty())
		{
			return;
		}
		worldState.serverMessages.push_back(message);
		while (worldState.serverMessages.size() > 8)
		{
			worldState.serverMessages.pop_front();
		}
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
	worldState.serverMessages.clear();
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
		if (event.type == WorldClientEvent::Type::ServerMessageReceived)
		{
			pushServerMessage(worldState, event.serverMessage);
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
