#include <server/core/ServerLaunch.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
	bool envFlagEnabled(const char *name)
	{
		const char *value = std::getenv(name);
		if (value == nullptr)
		{
			return false;
		}
		if (value[0] == '\0')
		{
			return false;
		}
		if (value[0] == '0' && value[1] == '\0')
		{
			return false;
		}
		return true;
	}

	bool tryReadEnvInt(const char *name, int &value)
	{
		const char *rawValue = std::getenv(name);
		if (rawValue == nullptr)
		{
			return false;
		}
		if (rawValue[0] == '\0')
		{
			return false;
		}

		char *end = nullptr;
		long parsed = std::strtol(rawValue, &end, 10);
		if (end == rawValue)
		{
			return false;
		}
		if (end == nullptr || *end != '\0')
		{
			return false;
		}

		value = static_cast<int>(parsed);
		return true;
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

	std::string defaultPlayerDatabasePath(WorldGenerationMode generationMode)
	{
		if (generationMode == WorldGenerationMode::ClassicStreaming)
		{
			return "voxplace_players_classic_gen.sqlite3";
		}
		return "voxplace_players_classic_voxplace.sqlite3";
	}

	std::string defaultWorldDatabasePath(WorldGenerationMode generationMode)
	{
		if (generationMode == WorldGenerationMode::ClassicStreaming)
		{
			return "voxplace_world_classic_gen.sqlite3";
		}
		return "voxplace_world_classic_voxplace.sqlite3";
	}

}

void printServerUsage(const char *programName)
{
	std::cout << "Usage: " << programName << " [--classic-gen] [--port <port>] [--db <path>] [--world-db <path>] [--modified-only-world] [--help]" << std::endl;
	std::cout << "  --classic-gen  Enable classic streaming generation around player movement" << std::endl;
	std::cout << "  --port <port>  Override server listen port (default: " << DEFAULT_SERVER_PORT << ")" << std::endl;
	std::cout << "  --db <path>    SQLite file for player persistence" << std::endl;
	std::cout << "  --world-db <path> SQLite file for world chunk persistence" << std::endl;
	std::cout << "  --modified-only-world  Persist only chunks modified by players" << std::endl;
	std::cout << "  --help         Show this help message" << std::endl;
}

ServerLaunchParseResult parseServerLaunchOptions(int argc, char **argv, ServerLaunchOptions &options)
{
	bool playerDatabasePathOverridden = false;
	bool worldDatabasePathOverridden = false;

	for (int argumentIndex = 1; argumentIndex < argc; argumentIndex++)
	{
		std::string_view argument = argv[argumentIndex];
		if (argument == "--classic-gen")
		{
			options.generationMode = WorldGenerationMode::ClassicStreaming;
			continue;
		}
		if (argument == "--help" || argument == "-h")
		{
			printServerUsage(argv[0]);
			return ServerLaunchParseResult::ExitSuccess;
		}
		if (argument == "--port")
		{
			argumentIndex++;
			if (argumentIndex >= argc)
			{
				std::cerr << "Missing value after --port" << std::endl;
				printServerUsage(argv[0]);
				return ServerLaunchParseResult::Error;
			}
			if (!parsePort(argv[argumentIndex], options.port))
			{
				std::cerr << "Invalid port: " << argv[argumentIndex] << std::endl;
				printServerUsage(argv[0]);
				return ServerLaunchParseResult::Error;
			}
			continue;
		}
		if (argument == "--db")
		{
			argumentIndex++;
			if (argumentIndex >= argc)
			{
				std::cerr << "Missing value after --db" << std::endl;
				printServerUsage(argv[0]);
				return ServerLaunchParseResult::Error;
			}
			options.playerDatabasePath = argv[argumentIndex];
			playerDatabasePathOverridden = true;
			continue;
		}
			if (argument == "--world-db")
			{
			argumentIndex++;
			if (argumentIndex >= argc)
			{
				std::cerr << "Missing value after --world-db" << std::endl;
				printServerUsage(argv[0]);
				return ServerLaunchParseResult::Error;
			}
			options.worldDatabasePath = argv[argumentIndex];
				worldDatabasePathOverridden = true;
				continue;
			}
			if (argument == "--modified-only-world")
			{
				options.persistGeneratedChunks = false;
				continue;
			}
			std::cerr << "Unknown argument: " << argument << std::endl;
			printServerUsage(argv[0]);
		return ServerLaunchParseResult::Error;
	}

	if (!playerDatabasePathOverridden)
	{
		options.playerDatabasePath = defaultPlayerDatabasePath(options.generationMode);
	}
	if (!worldDatabasePathOverridden)
	{
		options.worldDatabasePath = defaultWorldDatabasePath(options.generationMode);
	}

	return ServerLaunchParseResult::Success;
}

ServerEnvironmentOptions loadServerEnvironmentOptions()
{
	ServerEnvironmentOptions options;
	options.profileWorkersEnabled = envFlagEnabled("VOXPLACE_PROFILE_WORKERS");

	int overrideWorkers = 0;
	if (tryReadEnvInt("VOXPLACE_SERVER_WORKERS", overrideWorkers))
	{
		if (overrideWorkers > 0)
		{
			options.requestedWorkerCount = static_cast<size_t>(overrideWorkers);
		}
	}

	return options;
}
