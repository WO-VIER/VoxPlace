#ifndef CLIENT_CORE_CLIENT_LAUNCH_H
#define CLIENT_CORE_CLIENT_LAUNCH_H

#include <client/core/GameState.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct ClientEnvironmentOptions
{
	bool profileWorkersEnabled = false;
	size_t requestedMeshWorkers = 0;
	size_t classicMaxInflightChunkRequests = 192;
	size_t classicMaxChunkRequestsPerFrame = 16;
	bool benchFlyEnabled = false;
	float benchFlySpeed = 220.0f;
	float benchDurationSeconds = 0.0f;
	bool sortVisibleChunksFrontToBack = true;
};

const char *terrainRenderArchitectureName(TerrainRenderArchitecture architecture);
bool envFlagEnabled(const char *name);
bool tryReadEnvInt(const char *name, int &value);
bool tryReadEnvFloat(const char *name, float &value);
void printClientUsage(const char *programName);
bool parseClientPort(std::string_view rawPort, uint16_t &port);
bool parseClientLaunchOptions(int argc, char **argv, ClientLaunchOptions &options);
ClientEnvironmentOptions loadClientEnvironmentOptions(GameState &gameState);

#endif
