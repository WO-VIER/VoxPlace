#include <WorldClient.h>

#include <VoxelChunkData.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
	struct PregenOptions
	{
		std::string host;
		uint16_t port = 0;
		std::string username;
		std::string password;
		int radiusChunks = 0;
		int centerChunkX = 0;
		int centerChunkZ = 0;
		size_t maxInflight = 128;
	};

	void printUsage(const char *programName)
	{
		std::cout
			<< "Usage: " << programName
			<< " <host> <port> <username> <password> <radius_chunks> [center_chunk_x center_chunk_z] [max_inflight]"
			<< std::endl;
		std::cout
			<< "Example: " << programName
			<< " 161.35.214.248 28713 PregenUser StrongPass 64 0 0 128"
			<< std::endl;
	}

	bool parsePort(const char *rawPort, uint16_t &port)
	{
		if (rawPort == nullptr || rawPort[0] == '\0')
		{
			return false;
		}

		char *end = nullptr;
		long parsed = std::strtol(rawPort, &end, 10);
		if (end == rawPort || end == nullptr || *end != '\0')
		{
			return false;
		}
		if (parsed <= 0 || parsed > 65535)
		{
			return false;
		}

		port = static_cast<uint16_t>(parsed);
		return true;
	}

	bool parseInt(const char *rawValue, int &value)
	{
		if (rawValue == nullptr || rawValue[0] == '\0')
		{
			return false;
		}

		char *end = nullptr;
		long parsed = std::strtol(rawValue, &end, 10);
		if (end == rawValue || end == nullptr || *end != '\0')
		{
			return false;
		}

		value = static_cast<int>(parsed);
		return true;
	}

	bool parseSizeT(const char *rawValue, size_t &value)
	{
		if (rawValue == nullptr || rawValue[0] == '\0')
		{
			return false;
		}

		char *end = nullptr;
		unsigned long parsed = std::strtoul(rawValue, &end, 10);
		if (end == rawValue || end == nullptr || *end != '\0')
		{
			return false;
		}

		value = static_cast<size_t>(parsed);
		return true;
	}

	bool parseOptions(int argc, char **argv, PregenOptions &options)
	{
		if (argc != 6 && argc != 8 && argc != 9)
		{
			printUsage(argv[0]);
			return false;
		}

		options.host = argv[1];
		if (!parsePort(argv[2], options.port))
		{
			std::cerr << "Invalid port: " << argv[2] << std::endl;
			return false;
		}

		options.username = argv[3];
		options.password = argv[4];
		if (options.username.empty() || options.password.empty())
		{
			std::cerr << "Username and password must not be empty" << std::endl;
			return false;
		}

		if (!parseInt(argv[5], options.radiusChunks) || options.radiusChunks < 0)
		{
			std::cerr << "Invalid radius_chunks: " << argv[5] << std::endl;
			return false;
		}

		if (argc >= 8)
		{
			if (!parseInt(argv[6], options.centerChunkX) ||
				!parseInt(argv[7], options.centerChunkZ))
			{
				std::cerr << "Invalid center chunk coordinates" << std::endl;
				return false;
			}
		}

		if (argc >= 9)
		{
			if (!parseSizeT(argv[8], options.maxInflight) || options.maxInflight == 0)
			{
				std::cerr << "Invalid max_inflight: " << argv[8] << std::endl;
				return false;
			}
		}

		return true;
	}

	std::vector<ChunkCoord> buildSquareSpiral(int radiusChunks, int centerChunkX, int centerChunkZ)
	{
		std::vector<ChunkCoord> coords;
		size_t side = static_cast<size_t>(radiusChunks * 2 + 1);
		coords.reserve(side * side);
		coords.push_back(ChunkCoord{centerChunkX, centerChunkZ});

		for (int ring = 1; ring <= radiusChunks; ring++)
		{
			int minX = centerChunkX - ring;
			int maxX = centerChunkX + ring;
			int minZ = centerChunkZ - ring;
			int maxZ = centerChunkZ + ring;

			for (int x = minX; x <= maxX; x++)
			{
				coords.push_back(ChunkCoord{x, minZ});
			}
			for (int z = minZ + 1; z <= maxZ; z++)
			{
				coords.push_back(ChunkCoord{maxX, z});
			}
			for (int x = maxX - 1; x >= minX; x--)
			{
				coords.push_back(ChunkCoord{x, maxZ});
			}
			for (int z = maxZ - 1; z > minZ; z--)
			{
				coords.push_back(ChunkCoord{minX, z});
			}
		}

		return coords;
	}
}

int main(int argc, char **argv)
{
	PregenOptions options;
	if (!parseOptions(argc, argv, options))
	{
		return 1;
	}

	std::vector<ChunkCoord> coords = buildSquareSpiral(
		options.radiusChunks,
		options.centerChunkX,
		options.centerChunkZ);
	if (coords.empty())
	{
		std::cerr << "No chunk coordinates generated" << std::endl;
		return 1;
	}

	std::cout << "Connecting pregen client to "
			  << options.host << ":" << options.port
			  << " radius=" << options.radiusChunks
			  << " center=(" << options.centerChunkX << "," << options.centerChunkZ << ")"
			  << " total_chunks=" << coords.size()
			  << " max_inflight=" << options.maxInflight
			  << std::endl;

	WorldClient worldClient;
	if (!worldClient.connectToServer(
			options.host,
			options.port,
			options.username,
			options.password))
	{
		std::cerr << "Failed to connect: "
				  << worldClient.lastConnectionError() << std::endl;
		return 1;
	}

	std::unordered_set<int64_t> inflightKeys;
	inflightKeys.reserve(options.maxInflight * 2);
	std::unordered_set<int64_t> receivedKeys;
	receivedKeys.reserve(coords.size() * 2);

	size_t nextIndex = 0;
	size_t receivedCount = 0;
	auto startTime = std::chrono::steady_clock::now();
	auto lastProgressLog = startTime;

	while (receivedCount < coords.size())
	{
		while (nextIndex < coords.size() && inflightKeys.size() < options.maxInflight)
		{
			const ChunkCoord &coord = coords[nextIndex];
			int64_t key = chunkKey(coord);
			if (receivedKeys.find(key) != receivedKeys.end() ||
				inflightKeys.find(key) != inflightKeys.end())
			{
				nextIndex++;
				continue;
			}

			worldClient.sendChunkRequest(coord.x, coord.z);
			inflightKeys.insert(key);
			nextIndex++;
		}

		worldClient.service();

		WorldClientEvent event;
		bool hadEvent = false;
		while (worldClient.popEvent(event))
		{
			hadEvent = true;
			if (event.type == WorldClientEvent::Type::Disconnected)
			{
				std::cerr << "Disconnected during pregen" << std::endl;
				return 1;
			}
			if (event.type != WorldClientEvent::Type::ChunkReceived)
			{
				continue;
			}

			int64_t key = chunkKey(event.chunk.chunkX, event.chunk.chunkZ);
			if (!receivedKeys.insert(key).second)
			{
				continue;
			}

			inflightKeys.erase(key);
			receivedCount++;
			worldClient.sendChunkDrop(event.chunk.chunkX, event.chunk.chunkZ);
		}

		auto now = std::chrono::steady_clock::now();
		auto sinceLastLog = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressLog);
		if (sinceLastLog.count() >= 1000 || receivedCount == coords.size())
		{
			double elapsedSeconds = static_cast<double>(
				std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count()) /
				1000.0;
			double chunksPerSecond = 0.0;
			if (elapsedSeconds > 0.0)
			{
				chunksPerSecond = static_cast<double>(receivedCount) / elapsedSeconds;
			}

			std::cout << "[pregen] received=" << receivedCount
					  << "/" << coords.size()
					  << " inflight=" << inflightKeys.size()
					  << " requested=" << nextIndex
					  << " chunks_per_sec=" << chunksPerSecond
					  << std::endl;
			lastProgressLog = now;
		}

		if (!hadEvent)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	double totalSeconds = static_cast<double>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - startTime)
			.count()) /
		1000.0;
	double chunksPerSecond = 0.0;
	if (totalSeconds > 0.0)
	{
		chunksPerSecond = static_cast<double>(receivedCount) / totalSeconds;
	}

	std::cout << "Pregen complete: received=" << receivedCount
			  << " total_seconds=" << totalSeconds
			  << " chunks_per_sec=" << chunksPerSecond
			  << std::endl;

	worldClient.disconnect();
	return 0;
}
