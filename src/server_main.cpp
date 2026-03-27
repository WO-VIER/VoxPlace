#include <TerrainChunkGenerator.h>
#include <WorldServer.h>

#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	constexpr uint16_t DEFAULT_SERVER_PORT = 28713;

	void printUsage(const char *programName)
	{
		std::cout << "Usage: " << programName << " [--classic-gen] [--port <port>] [--db <path>] [--help]" << std::endl;
		std::cout << "  --classic-gen  Enable classic streaming generation around player movement" << std::endl;
		std::cout << "  --port <port>  Override server listen port (default: " << DEFAULT_SERVER_PORT << ")" << std::endl;
		std::cout << "  --db <path>    SQLite file for player persistence" << std::endl;
		std::cout << "  --help         Show this help message" << std::endl;
	}

	bool parsePort(std::string_view rawPort, uint16_t &port)
	{
		if (rawPort.empty())
		{
			return false;
		}

		uint32_t parsed = 0;
		for (char character : rawPort)
		{
			if (character < '0' || character > '9')
			{
				return false;
			}
			parsed *= 10;
			parsed += static_cast<uint32_t>(character - '0');
			if (parsed > 65535)
			{
				return false;
			}
		}

		if (parsed == 0)
		{
			return false;
		}

		port = static_cast<uint16_t>(parsed);
		return true;
	}

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

	WorldGenerationMode generationMode = WorldGenerationMode::ActivityFrontier;
	uint16_t port = DEFAULT_SERVER_PORT;
	std::string playerDatabasePath = "voxplace_players.sqlite3";
	bool playerDatabasePathOverridden = false;

	for (int argumentIndex = 1; argumentIndex < argc; argumentIndex++)
	{
		std::string_view argument = argv[argumentIndex];
		if (argument == "--classic-gen")
		{
			generationMode = WorldGenerationMode::ClassicStreaming;
			continue;
		}
		if (argument == "--help" || argument == "-h")
		{
			printUsage(argv[0]);
			return 0;
		}
		if (argument == "--port")
		{
			argumentIndex++;
			if (argumentIndex >= argc)
			{
				std::cerr << "Missing value after --port" << std::endl;
				printUsage(argv[0]);
				return 1;
			}
			if (!parsePort(argv[argumentIndex], port))
			{
				std::cerr << "Invalid port: " << argv[argumentIndex] << std::endl;
				printUsage(argv[0]);
				return 1;
			}
			continue;
		}
		if (argument == "--db")
		{
			argumentIndex++;
			if (argumentIndex >= argc)
			{
				std::cerr << "Missing value after --db" << std::endl;
				printUsage(argv[0]);
				return 1;
			}
			playerDatabasePath = argv[argumentIndex];
			playerDatabasePathOverridden = true;
			continue;
		}

		std::cerr << "Unknown argument: " << argument << std::endl;
		printUsage(argv[0]);
		return 1;
	}

	if (!playerDatabasePathOverridden)
	{
		if (generationMode == WorldGenerationMode::ClassicStreaming)
		{
			playerDatabasePath = "voxplace_players_classic_gen.sqlite3";
		}
		else
		{
			playerDatabasePath = "voxplace_players_classic_voxplace.sqlite3";
		}
	}

	std::cout << "World generation mode: "
			  << worldGenerationModeName(generationMode) << std::endl;
	std::cout << "Server port: " << port << std::endl;
	std::cout << "Player DB: " << playerDatabasePath << std::endl;

	WorldServer server(
		port,
		std::make_unique<TerrainChunkGenerator>(42),
		generationMode,
		playerDatabasePath);
	if (!server.start())
	{
		std::cerr << "Failed to start VoxPlaceServer" << std::endl;
		return 1;
	}
	return server.run();
}
