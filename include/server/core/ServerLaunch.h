#ifndef SERVER_CORE_SERVER_LAUNCH_H
#define SERVER_CORE_SERVER_LAUNCH_H

#include <WorldBounds.h>

#include <cstddef>
#include <cstdint>
#include <string>

constexpr uint16_t DEFAULT_SERVER_PORT = 28713;

struct ServerLaunchOptions
{
	WorldGenerationMode generationMode = WorldGenerationMode::ActivityFrontier;
	uint16_t port = DEFAULT_SERVER_PORT;
	std::string playerDatabasePath = "voxplace_players.sqlite3";
	std::string worldDatabasePath = "voxplace_world.sqlite3";
	bool persistGeneratedChunks = false;
};

struct ServerEnvironmentOptions
{
	bool profileWorkersEnabled = false;
	size_t requestedWorkerCount = 0;
	uint32_t streamTickMs = 10;
};

enum class ServerLaunchParseResult
{
	Success = 0,
	ExitSuccess = 1,
	Error = 2
};

void printServerUsage(const char *programName);
ServerLaunchParseResult parseServerLaunchOptions(int argc, char **argv, ServerLaunchOptions &options);
ServerEnvironmentOptions loadServerEnvironmentOptions();

#endif
