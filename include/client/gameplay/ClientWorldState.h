#ifndef CLIENT_GAMEPLAY_CLIENT_WORLD_STATE_H
#define CLIENT_GAMEPLAY_CLIENT_WORLD_STATE_H

#include <ClientChunk.h>
#include <WorldProtocol.h>
#include <WorldBounds.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct ClientChatMessage
{
	ServerChatMessageKind kind = ServerChatMessageKind::System;
	std::string username;
	std::string text;
	uint64_t receivedAtMs = 0;
};

struct ClientWorldState
{
	WorldFrontier frontier;
	bool hasWorldFrontier = false;
	bool hasExpansionStatus = false;
	bool hasServerProfile = false;
	bool hasPreviousCameraPosition = false;
	glm::vec3 previousCameraPosition = glm::vec3(0.0f);
	std::unordered_map<int64_t, ClientChunk *> chunkMap;
	std::unordered_set<int64_t> streamedChunkKeys;
	std::unordered_map<int64_t, uint64_t> pendingMeshRevisions;
	ExpansionStatusMessage expansionStatus;
	ServerProfileMessage serverProfile;
	std::deque<ClientChatMessage> chatMessages;
	size_t profileChunkRequestsWindow = 0;
	size_t profileChunkDropsWindow = 0;
	size_t profileChunkReceivesWindow = 0;
	size_t profileChunkUnloadsWindow = 0;
	size_t profileMeshedChunkCountWindow = 0;
	size_t profileMeshedSectionCountWindow = 0;
};

#endif
