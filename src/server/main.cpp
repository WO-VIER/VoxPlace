#include <TerrainChunkGenerator.h>
#include <WorldServer.h>
#include <server/core/ServerLaunch.h>

#include <csignal>
#include <iostream>
#include <memory>

namespace
{
	void handleStopSignal(int)
	{
		requestWorldServerSignalStop();
	}
}

int main(int argc, char **argv)
{
	resetWorldServerSignalStop();
	std::signal(SIGINT, handleStopSignal);
	std::signal(SIGTERM, handleStopSignal);

	ServerLaunchOptions launchOptions;
	ServerLaunchParseResult parseResult = parseServerLaunchOptions(argc, argv, launchOptions);
	if (parseResult == ServerLaunchParseResult::ExitSuccess)
	{
		return 0;
	}
	if (parseResult == ServerLaunchParseResult::Error)
	{
		return 1;
	}

	ServerEnvironmentOptions environmentOptions = loadServerEnvironmentOptions();

	std::cout << "World generation mode: "
			  << worldGenerationModeName(launchOptions.generationMode) << std::endl;
	std::cout << "Server port: " << launchOptions.port << std::endl;
	std::cout << "Player DB: " << launchOptions.playerDatabasePath << std::endl;
	if (environmentOptions.requestedWorkerCount > 0)
	{
		std::cout << "Server worker override: " << environmentOptions.requestedWorkerCount << std::endl;
	}
	if (environmentOptions.profileWorkersEnabled)
	{
		std::cout << "Server worker profiling requested via environment" << std::endl;
	}
	std::cout << "World DB: " << launchOptions.worldDatabasePath << std::endl;
	std::cout << "Persistence mode: "
			  << (launchOptions.persistGeneratedChunks ? "full-db" : "modified-only")
			  << std::endl;

	WorldServer server(
		launchOptions.port,
		std::make_unique<TerrainChunkGenerator>(42),
		launchOptions.generationMode,
		launchOptions.playerDatabasePath,
		launchOptions.worldDatabasePath,
		launchOptions.persistGeneratedChunks,
		environmentOptions);
	if (!server.start())
	{
		std::cerr << "Failed to start VoxPlaceServer" << std::endl;
		return 1;
	}
	return server.run();
}
