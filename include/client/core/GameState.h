#ifndef CLIENT_CORE_GAME_STATE_H
#define CLIENT_CORE_GAME_STATE_H

#include <client/rendering/RenderSettings.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

constexpr uint16_t CLIENT_DEFAULT_SERVER_PORT = 28713;
constexpr const char *CLIENT_DEFAULT_SERVER_HOST = "127.0.0.1";

enum class ClientAppState
{
	Login = 0,
	Connecting = 1,
	InGame = 2
};

struct ClientLaunchOptions
{
	std::string host = CLIENT_DEFAULT_SERVER_HOST;
	uint16_t port = CLIENT_DEFAULT_SERVER_PORT;
	std::string username;
	std::string password;
	bool autoConnect = false;
};

struct ClientConnectionState
{
	std::string serverHost = CLIENT_DEFAULT_SERVER_HOST;
	uint16_t serverPort = CLIENT_DEFAULT_SERVER_PORT;
	std::string playerUsername;
};

struct MovementSyncState
{
	glm::vec3 lastSentPosition = glm::vec3(0.0f, 35.0f, 0.0f);
	glm::vec3 lastSentLookDirection = glm::vec3(0.0f, 0.0f, -1.0f);
	uint64_t lastSentAtMs = 0;
};

struct ClientDisplayState
{
	int screenWidth = 1920;
	int screenHeight = 1080;
};

struct ClientInputState
{
	float lastMouseX = static_cast<float>(1920) / 2.0f;
	float lastMouseY = static_cast<float>(1080) / 2.0f;
	bool firstMouse = true;
	bool placeBlockRequested = false;
	bool breakBlockRequested = false;
};

struct GameState
{
	ClientDisplayState display;
	ClientInputState input;
	ClientConnectionState connection;
	MovementSyncState movementSync;
	ClientAppState appState = ClientAppState::Login;
	RenderSettings render;
	TerrainRenderArchitecture terrainRenderArchitecture = TerrainRenderArchitecture::ChunkSsboDirect;
	TerrainRenderArchitecture previousTerrainRenderArchitecture = TerrainRenderArchitecture::ChunkSsboDirect;
	bool debugOverlayVisible = false;
};

#endif
