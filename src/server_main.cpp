#include <TerrainChunkGenerator.h>
#include <WorldServer.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
	constexpr uint16_t DEFAULT_SERVER_PORT = 28713;

	void printUsage(const char *programName)
	{
		std::cout << "Usage: " << programName << " [--classic-gen] [--help]" << std::endl;
		std::cout << "  --classic-gen  Enable classic streaming generation around player movement" << std::endl;
		std::cout << "  --help         Show this help message" << std::endl;
	}
}

int main(int argc, char **argv)
{
	WorldGenerationMode generationMode = WorldGenerationMode::ActivityFrontier;

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

		std::cerr << "Unknown argument: " << argument << std::endl;
		printUsage(argv[0]);
		return 1;
	}

	std::cout << "World generation mode: "
			  << worldGenerationModeName(generationMode) << std::endl;

	WorldServer server(
		DEFAULT_SERVER_PORT,
		std::make_unique<TerrainChunkGenerator>(42),
		generationMode);
	if (!server.start())
	{
		std::cerr << "Failed to start VoxPlaceServer" << std::endl;
		return 1;
	}
	return server.run();
}
