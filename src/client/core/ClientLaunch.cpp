#include <client/core/ClientLaunch.h>

#include <PlayerUsername.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

const char *terrainRenderArchitectureName(TerrainRenderArchitecture architecture)
{
	switch (architecture)
	{
	case TerrainRenderArchitecture::ChunkSsboDirect:
		return "Chunk SSBO Direct";
	case TerrainRenderArchitecture::BigGpuBufferIndirect:
		return "Big GPU Buffer Indirect";
	}
	return "Unknown";
}

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

bool tryReadEnvFloat(const char *name, float &value)
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
	float parsed = std::strtof(rawValue, &end);
	if (end == rawValue)
	{
		return false;
	}
	if (end == nullptr || *end != '\0')
	{
		return false;
	}

	value = parsed;
	return true;
}

void printClientUsage(const char *programName)
{
	std::cout << "Usage: " << programName << " [server_host server_port username]" << std::endl;
	std::cout << "Example: " << programName << " 127.0.0.1 28713 Player" << std::endl;
	std::cout << "         " << programName << " 192.168.1.42 28713 Player" << std::endl;
}

bool parseClientPort(std::string_view rawPort, uint16_t &port)
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

bool parseClientLaunchOptions(int argc, char **argv, ClientLaunchOptions &options)
{
	if (argc == 1)
	{
		return true;
	}

	if (argc != 4)
	{
		printClientUsage(argv[0]);
		return false;
	}

	options.host = std::string(argv[1]);
	if (options.host.empty())
	{
		std::cerr << "Server host must not be empty" << std::endl;
		return false;
	}

	if (!parseClientPort(argv[2], options.port))
	{
		std::cerr << "Invalid server port: " << argv[2] << std::endl;
		return false;
	}

	options.username = trimPlayerUsername(argv[3]);
	PlayerUsernameValidationError usernameError = validatePlayerUsername(options.username);
	if (usernameError != PlayerUsernameValidationError::None)
	{
		std::cerr << "Invalid username: "
				  << playerUsernameValidationErrorText(usernameError)
				  << std::endl;
		return false;
	}

	options.autoConnect = true;
	return true;
}

ClientEnvironmentOptions loadClientEnvironmentOptions(GameState &gameState)
{
	ClientEnvironmentOptions options;

	int envRenderDistance = 0;
	if (tryReadEnvInt("VOXPLACE_RENDER_DISTANCE", envRenderDistance))
	{
		envRenderDistance = std::clamp(envRenderDistance, 2, 32);
		gameState.render.renderDistanceChunks = envRenderDistance;
	}

	int envClassicPadding = 0;
	if (tryReadEnvInt("VOXPLACE_CLASSIC_STREAM_PAD", envClassicPadding))
	{
		envClassicPadding = std::clamp(envClassicPadding, 0, 8);
		gameState.render.classicStreamingPaddingChunks = envClassicPadding;
	}

	int envClassicMaxInflightRequests = 0;
	if (tryReadEnvInt("VOXPLACE_CLASSIC_MAX_INFLIGHT_REQUESTS", envClassicMaxInflightRequests))
	{
		if (envClassicMaxInflightRequests > 0)
		{
			options.classicMaxInflightChunkRequests = static_cast<size_t>(envClassicMaxInflightRequests);
		}
	}

	int envClassicMaxRequestsPerFrame = 0;
	if (tryReadEnvInt("VOXPLACE_CLASSIC_MAX_REQUESTS_PER_FRAME", envClassicMaxRequestsPerFrame))
	{
		if (envClassicMaxRequestsPerFrame > 0)
		{
			options.classicMaxChunkRequestsPerFrame = static_cast<size_t>(envClassicMaxRequestsPerFrame);
		}
	}

	int envMeshWorkers = 0;
	if (tryReadEnvInt("VOXPLACE_MESH_WORKERS", envMeshWorkers))
	{
		if (envMeshWorkers > 0)
		{
			options.requestedMeshWorkers = static_cast<size_t>(envMeshWorkers);
		}
	}

	options.profileWorkersEnabled = envFlagEnabled("VOXPLACE_PROFILE_WORKERS");
	options.benchFlyEnabled = envFlagEnabled("VOXPLACE_BENCH_FLY");

	float envBenchFlySpeed = 0.0f;
	if (tryReadEnvFloat("VOXPLACE_BENCH_FLY_SPEED", envBenchFlySpeed))
	{
		if (envBenchFlySpeed > 0.0f)
		{
			options.benchFlySpeed = envBenchFlySpeed;
		}
	}

	float envBenchDuration = 0.0f;
	if (tryReadEnvFloat("VOXPLACE_BENCH_SECONDS", envBenchDuration))
	{
		if (envBenchDuration > 0.0f)
		{
			options.benchDurationSeconds = envBenchDuration;
		}
	}

	int envSortVisibleChunks = 0;
	if (tryReadEnvInt("VOXPLACE_SORT_VISIBLE_CHUNKS", envSortVisibleChunks))
	{
		if (envSortVisibleChunks == 0)
		{
			options.sortVisibleChunksFrontToBack = false;
		}
	}

	return options;
}
