#include <WorldServer.h>

#include <ChunkPalette.h>

#include <enet/enet.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	constexpr size_t WORLD_CHANNEL_RELIABLE = 0;
	constexpr int SERVER_TICK_MS = 50;
	constexpr size_t DEFAULT_MAX_INTEGRATED_CHUNKS_PER_TICK = 4;
	constexpr size_t DEFAULT_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK = 5;
	constexpr size_t CLASSIC_MIN_INTEGRATED_CHUNKS_PER_TICK = 8;
	constexpr size_t CLASSIC_MID_INTEGRATED_CHUNKS_PER_TICK = 16;
	constexpr size_t CLASSIC_MAX_INTEGRATED_CHUNKS_PER_TICK = 32;
	constexpr size_t CLASSIC_MIN_CHUNK_SENDS_PER_CLIENT_PER_TICK = 8;
	constexpr size_t CLASSIC_MID_CHUNK_SENDS_PER_CLIENT_PER_TICK = 16;
	constexpr size_t CLASSIC_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK = 24;
	constexpr int INITIAL_PLAYABLE_RADIUS = 1;
	constexpr int INITIAL_PADDING_CHUNKS = 0;

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

	size_t computeWorkerCount()
	{
		int overrideWorkers = 0;
		if (tryReadEnvInt("VOXPLACE_SERVER_WORKERS", overrideWorkers))
		{
			if (overrideWorkers > 0)
			{
				return static_cast<size_t>(overrideWorkers);
			}
		}

		unsigned int reported = std::thread::hardware_concurrency();
		printf("Threads %d\n", reported);
		if (reported == 0)
		{
			return 2;
		}

		// Heuristique mesurée pour le serveur génération:
		// en classic-gen local avec gros churn, 8 workers sur-produisent surtout
		// des readyChunks sans améliorer clairement le résultat visible client.
		// 2 workers tiennent déjà bien le flux sur le desktop principal, et
		// correspondent mieux à un budget partagé client+serveur.
		//
		// On garde donc:
		// - petite machine (<= 8 threads logiques): 1 worker
		// - machine desktop courante (<= 16 threads logiques): 2 workers
		// - machine plus grosse: 4 workers par défaut
		//
		// Les overrides env restent possibles pour les benchs ou un serveur dédié.
		if (reported <= 8)
		{
			return 1;
		}
		if (reported <= 16)
		{
			return 2;
		}
		return 4;
	}

	ChunkBounds makeSquareBounds(int radiusChunks)
	{
		ChunkBounds bounds;
		bounds.minChunkX = -radiusChunks;
		bounds.maxChunkXExclusive = radiusChunks + 1;
		bounds.minChunkZ = -radiusChunks;
		bounds.maxChunkZExclusive = radiusChunks + 1;
		return bounds;
	}

	ChunkCoord chunkCoordFromKey(int64_t key)
	{
		ChunkCoord coord;
		coord.x = static_cast<int>(key >> 32);
		coord.z = static_cast<int>(key & 0xFFFFFFFF);
		return coord;
	}
}

struct WorldServer::Impl
{
	struct ClientSession
	{
		ENetPeer *peer = nullptr;
		std::unordered_set<int64_t> wantedChunks;
		std::unordered_set<int64_t> loadedChunks;
		std::unordered_set<int64_t> queuedChunks;
		std::deque<int64_t> sendQueue;
	};

	uint16_t port = 0;
	bool enetInitialized = false;
	ENetHost *host = nullptr;
	std::unique_ptr<IChunkGenerator> generator;
	WorldGenerationMode generationMode = WorldGenerationMode::ActivityFrontier;

	WorldFrontier frontier;
	std::unordered_map<int64_t, VoxelChunkData> worldChunks;
	std::unordered_set<int64_t> activeChunkKeys;
	std::unordered_map<ENetPeer *, ClientSession> clients;

	std::atomic<bool> running = false;
	std::mutex taskMutex;
	std::condition_variable taskCv;
	std::deque<ChunkCoord> generationTasks;
	std::unordered_set<int64_t> scheduledChunkKeys;
	std::mutex readyMutex;
	std::deque<VoxelChunkData> readyChunks;
	std::vector<std::thread> workers;
	size_t workerCount = 0;
	bool profileWorkers = false;
	std::chrono::steady_clock::time_point profileWindowStart;
	size_t profileGeneratedChunks = 0;
	size_t profileIntegratedChunks = 0;
	size_t profileTickCount = 0;
	size_t profileAccumGenerationTasks = 0;
	size_t profileAccumReadyChunks = 0;
	size_t profileMaxGenerationTasks = 0;
	size_t profileMaxReadyChunks = 0;
	size_t profileSnapshotCount = 0;
	size_t profileSnapshotPayloadBytes = 0;
	size_t profileSnapshotRawBytes = 0;
	size_t profileSnapshotSectionCount = 0;

	Impl(uint16_t listenPort,
		 std::unique_ptr<IChunkGenerator> worldGenerator,
		 WorldGenerationMode selectedGenerationMode)
		: port(listenPort),
		  generator(std::move(worldGenerator)),
		  generationMode(selectedGenerationMode)
	{
		frontier.playableBounds = makeSquareBounds(INITIAL_PLAYABLE_RADIUS);
		frontier.paddingChunks = INITIAL_PADDING_CHUNKS;
		frontier.generatedBounds = frontier.playableBounds.expanded(frontier.paddingChunks);
		frontier.mode = generationMode;
		updateExpansionProgress();
		profileWorkers = envFlagEnabled("VOXPLACE_PROFILE_WORKERS");
	}

	~Impl()
	{
		stop();
	}

	bool start()
	{
		if (enet_initialize() != 0)
		{
			return false;
		}
		enetInitialized = true;

		ENetAddress address{};
		address.host = ENET_HOST_ANY;
		address.port = port;
		host = enet_host_create(&address, 32, 2, 0, 0);
		if (host == nullptr)
		{
			return false;
		}

		running = true;
		workerCount = computeWorkerCount();
		for (size_t i = 0; i < workerCount; i++)
		{
			workers.emplace_back(&Impl::workerLoop, this);
		}
		profileWindowStart = std::chrono::steady_clock::now();

		std::cout << "WorldServer listening on port " << port
				  << " with " << workerCount << " generation worker(s)"
				  << " in " << worldGenerationModeName(generationMode)
				  << " mode" << std::endl;
		if (profileWorkers)
		{
			std::cout << "Server worker profiling enabled" << std::endl;
		}

		bootstrapInitialWorld();
		return true;
	}

	void stop()
	{
		if (!running)
		{
			cleanupNetwork();
			return;
		}

		running = false;
		taskCv.notify_all();
		for (std::thread &worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
		workers.clear();
		cleanupNetwork();
	}

	void cleanupNetwork()
	{
		if (host != nullptr)
		{
			enet_host_destroy(host);
			host = nullptr;
		}
		if (enetInitialized)
		{
			enet_deinitialize();
			enetInitialized = false;
		}
	}

	int run()
	{
		using clock = std::chrono::steady_clock;
		auto nextTick = clock::now();

		while (running)
		{
			serviceNetwork(1);
			auto now = clock::now();
			if (now >= nextTick)
			{
				tick();
				nextTick = now + std::chrono::milliseconds(SERVER_TICK_MS);
			}
		}

		return 0;
	}

	void bootstrapInitialWorld()
	{
		const ChunkBounds &initialBounds = frontier.generatedBounds;
		size_t expectedChunkCount =
			static_cast<size_t>(initialBounds.widthChunks()) *
			static_cast<size_t>(initialBounds.depthChunks());

		scheduleBounds(initialBounds);

		while (worldChunks.size() < expectedChunkCount)
		{
			integrateReadyChunks(expectedChunkCount);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		std::cout << "Bootstrap complete: " << worldChunks.size()
				  << " generated chunk(s)" << std::endl;
	}

	int computeActivePlayableChunkCount() const
	{
		int activePlayableChunkCount = 0;
		for (int64_t key : activeChunkKeys)
		{
			ChunkCoord coord = chunkCoordFromKey(key);
			if (frontier.playableBounds.containsChunk(coord))
			{
				activePlayableChunkCount++;
			}
		}
		return activePlayableChunkCount;
	}

	void updateExpansionProgress()
	{
		if (generationMode == WorldGenerationMode::ClassicStreaming)
		{
			frontier.activePlayableChunkCount = 0;
			frontier.requiredActiveChunkCount = 0;
			return;
		}

		frontier.activePlayableChunkCount = computeActivePlayableChunkCount();
		frontier.requiredActiveChunkCount = perimeterChunkCount(frontier.playableBounds);
	}

	bool usesClassicStreaming() const
	{
		if (generationMode == WorldGenerationMode::ClassicStreaming)
		{
			return true;
		}
		return false;
	}

	bool canStreamChunkRequest(int chunkX, int chunkZ) const
	{
		if (usesClassicStreaming())
		{
			return true;
		}
		return frontier.generatedBounds.containsChunk(chunkX, chunkZ);
	}

	void workerLoop()
	{
		while (running)
		{
			ChunkCoord coord;
			{
				std::unique_lock<std::mutex> lock(taskMutex);
				taskCv.wait(lock, [&]()
							{ return !running || !generationTasks.empty(); });
				if (!running && generationTasks.empty())
				{
					return;
				}
				coord = generationTasks.front();
				generationTasks.pop_front();
			}

			VoxelChunkData chunk(coord.x, coord.z);
			generator->fillChunk(chunk);

			std::lock_guard<std::mutex> readyLock(readyMutex);
			readyChunks.push_back(std::move(chunk));
			profileGeneratedChunks++;
		}
	}

	void serviceNetwork(uint32_t timeoutMs)
	{
		if (host == nullptr)
		{
			return;
		}

		ENetEvent event{};
		while (enet_host_service(host, &event, timeoutMs) > 0)
		{
			timeoutMs = 0;
			if (event.type == ENET_EVENT_TYPE_CONNECT)
			{
				handleConnect(event.peer);
				continue;
			}
			if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			{
				handleDisconnect(event.peer);
				continue;
			}
			if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				handlePacket(event.peer, event.packet->data, event.packet->dataLength);
				enet_packet_destroy(event.packet);
			}
		}
	}

	void tick()
	{
		integrateReadyChunks(integratedChunksBudgetForTick());
		for (auto &entry : clients)
		{
			sendQueuedChunks(entry.second);
		}
		enet_host_flush(host);
		logWorkerProfileWindowIfNeeded();
	}

	void integrateReadyChunks(size_t maxCount)
	{
		size_t integratedCount = 0;

		while (integratedCount < maxCount)
		{
			VoxelChunkData chunk;
			{
				std::lock_guard<std::mutex> readyLock(readyMutex);
				if (readyChunks.empty())
				{
					break;
				}
				chunk = std::move(readyChunks.front());
				readyChunks.pop_front();
			}

			int64_t key = chunkKey(chunk.chunkX, chunk.chunkZ);
			worldChunks[key] = std::move(chunk);
			{
				std::lock_guard<std::mutex> taskLock(taskMutex);
				scheduledChunkKeys.erase(key);
			}
			for (auto &entry : clients)
			{
				ClientSession &session = entry.second;
				if (session.wantedChunks.find(key) != session.wantedChunks.end())
				{
					queueChunkForClient(session, key);
				}
			}
			integratedCount++;
		}
		profileIntegratedChunks += integratedCount;
	}

	void logWorkerProfileWindowIfNeeded()
	{
		if (!profileWorkers)
		{
			return;
		}

		size_t generationTaskCount = 0;
		{
			std::lock_guard<std::mutex> taskLock(taskMutex);
			generationTaskCount = generationTasks.size();
		}

		size_t readyChunkCount = 0;
		{
			std::lock_guard<std::mutex> readyLock(readyMutex);
			readyChunkCount = readyChunks.size();
		}

		profileTickCount++;
		profileAccumGenerationTasks += generationTaskCount;
		profileAccumReadyChunks += readyChunkCount;
		if (generationTaskCount > profileMaxGenerationTasks)
		{
			profileMaxGenerationTasks = generationTaskCount;
		}
		if (readyChunkCount > profileMaxReadyChunks)
		{
			profileMaxReadyChunks = readyChunkCount;
		}

		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			now - profileWindowStart);
		if (elapsed.count() < 2000)
		{
			return;
		}

		double avgTasks = 0.0;
		double avgReady = 0.0;
		if (profileTickCount > 0)
		{
			avgTasks = static_cast<double>(profileAccumGenerationTasks) / static_cast<double>(profileTickCount);
			avgReady = static_cast<double>(profileAccumReadyChunks) / static_cast<double>(profileTickCount);
		}

		std::cout << "[server-profile] workers=" << workerCount
				  << " mode=" << worldGenerationModeName(generationMode)
				  << " clients=" << clients.size()
				  << " world_chunks=" << worldChunks.size()
				  << " generated_window=" << profileGeneratedChunks
				  << " integrated_window=" << profileIntegratedChunks
				  << " tasks_now=" << generationTaskCount
				  << " tasks_avg=" << avgTasks
				  << " tasks_max=" << profileMaxGenerationTasks
				  << " ready_now=" << readyChunkCount
				  << " ready_avg=" << avgReady
				  << " ready_max=" << profileMaxReadyChunks;

		if (profileSnapshotCount > 0)
		{
			double avgSnapshotBytes = static_cast<double>(profileSnapshotPayloadBytes) / static_cast<double>(profileSnapshotCount);
			double avgRawBytes = static_cast<double>(profileSnapshotRawBytes) / static_cast<double>(profileSnapshotCount);
			double avgSections = static_cast<double>(profileSnapshotSectionCount) / static_cast<double>(profileSnapshotCount);
			double ratio = 1.0;
			if (profileSnapshotRawBytes > 0)
			{
				ratio = static_cast<double>(profileSnapshotPayloadBytes) /
						static_cast<double>(profileSnapshotRawBytes);
			}
			std::cout << " snapshot_count=" << profileSnapshotCount
					  << " snapshot_avg_bytes=" << avgSnapshotBytes
					  << " snapshot_avg_raw_bytes=" << avgRawBytes
					  << " snapshot_avg_sections=" << avgSections
					  << " snapshot_ratio=" << ratio;
		}

		std::cout
			<< std::endl;

		profileWindowStart = now;
		profileGeneratedChunks = 0;
		profileIntegratedChunks = 0;
		profileTickCount = 0;
		profileAccumGenerationTasks = 0;
		profileAccumReadyChunks = 0;
		profileMaxGenerationTasks = generationTaskCount;
		profileMaxReadyChunks = readyChunkCount;
		profileSnapshotCount = 0;
		profileSnapshotPayloadBytes = 0;
		profileSnapshotRawBytes = 0;
		profileSnapshotSectionCount = 0;
	}

	size_t integratedChunksBudgetForTick()
	{
		if (!usesClassicStreaming())
		{
			return DEFAULT_MAX_INTEGRATED_CHUNKS_PER_TICK;
		}

		size_t readyCount = 0;
		{
			std::lock_guard<std::mutex> readyLock(readyMutex);
			readyCount = readyChunks.size();
		}

		size_t scheduledCount = 0;
		{
			std::lock_guard<std::mutex> taskLock(taskMutex);
			scheduledCount = generationTasks.size();
		}

		size_t budget = CLASSIC_MIN_INTEGRATED_CHUNKS_PER_TICK;
		if (readyCount > 16 || scheduledCount > 64)
		{
			budget = CLASSIC_MID_INTEGRATED_CHUNKS_PER_TICK;
		}
		if (readyCount > 64 || scheduledCount > 256)
		{
			budget = CLASSIC_MAX_INTEGRATED_CHUNKS_PER_TICK;
		}
		return budget;
	}

	void handleConnect(ENetPeer *peer)
	{
		ClientSession session;
		session.peer = peer;
		clients[peer] = std::move(session);
		std::cout << "Client connected" << std::endl;
	}

	void handleDisconnect(ENetPeer *peer)
	{
		clients.erase(peer);
		std::cout << "Client disconnected" << std::endl;
	}

	void handlePacket(ENetPeer *peer, const uint8_t *data, size_t size)
	{
		if (size == 0)
		{
			return;
		}

		PacketType type = static_cast<PacketType>(data[0]);

		if (type == PacketType::Hello)
		{
			HelloMessage hello;
			if (!decodeHello(data, size, hello))
			{
				return;
			}
			sendReliable(peer, encodeHello(hello));
			sendReliable(peer, encodeWorldFrontier(frontier));
			return;
		}

		if (type == PacketType::ChunkRequest)
		{
			ChunkRequestMessage request;
			if (!decodeChunkRequest(data, size, request))
			{
				return;
			}
			handleChunkRequest(peer, request);
			return;
		}

		if (type == PacketType::ChunkDrop)
		{
			ChunkDropMessage drop;
			if (!decodeChunkDrop(data, size, drop))
			{
				return;
			}
			handleChunkDrop(peer, drop);
			return;
		}

		if (type == PacketType::BlockActionRequest)
		{
			BlockActionRequestMessage request;
			if (!decodeBlockActionRequest(data, size, request))
			{
				return;
			}
			handleBlockAction(request);
		}
	}

	void handleChunkRequest(ENetPeer *peer, const ChunkRequestMessage &request)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}

		if (!canStreamChunkRequest(request.chunkX, request.chunkZ))
		{
			return;
		}

		int64_t key = chunkKey(request.chunkX, request.chunkZ);
		ClientSession &session = sessionIt->second;
		session.wantedChunks.insert(key);

		auto worldIt = worldChunks.find(key);
		if (worldIt != worldChunks.end())
		{
			queueChunkForClient(session, key);
			return;
		}

		scheduleChunkGeneration(request.chunkX, request.chunkZ);
	}

	void handleChunkDrop(ENetPeer *peer, const ChunkDropMessage &drop)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}

		int64_t key = chunkKey(drop.chunkX, drop.chunkZ);
		ClientSession &session = sessionIt->second;
		session.wantedChunks.erase(key);
		session.loadedChunks.erase(key);
		session.queuedChunks.erase(key);
	}

	void handleBlockAction(const BlockActionRequestMessage &request)
	{
		if (request.worldY < 0 || request.worldY >= CHUNK_SIZE_Y)
		{
			return;
		}
		if (!usesClassicStreaming() &&
			!frontier.playableBounds.containsWorldBlock(request.worldX, request.worldZ))
		{
			return;
		}

		int cx = floorDiv(request.worldX, CHUNK_SIZE_X);
		int cz = floorDiv(request.worldZ, CHUNK_SIZE_Z);
		int lx = floorMod(request.worldX, CHUNK_SIZE_X);
		int lz = floorMod(request.worldZ, CHUNK_SIZE_Z);
		int64_t key = chunkKey(cx, cz);

		auto worldIt = worldChunks.find(key);
		if (worldIt == worldChunks.end())
		{
			return;
		}

		uint32_t finalColor = 0;
		if (request.action == BlockActionType::Place)
		{
			finalColor = playerPaletteColor(request.paletteIndex);
		}

		if (!worldIt->second.setBlockRaw(lx, request.worldY, lz, finalColor))
		{
			return;
		}

		BlockUpdateBroadcastMessage update;
		update.worldX = request.worldX;
		update.worldY = request.worldY;
		update.worldZ = request.worldZ;
		update.finalColor = finalColor;
		update.revision = worldIt->second.revision;
		broadcastReliable(encodeBlockUpdateBroadcast(update));

		if (usesClassicStreaming())
		{
			return;
		}

		int previousActiveCount = frontier.activePlayableChunkCount;
		activeChunkKeys.insert(key);
		updateExpansionProgress();

		if (frontier.activePlayableChunkCount != previousActiveCount)
		{
			broadcastReliable(encodeWorldFrontier(frontier));
			std::cout << "Expansion progress: "
					  << frontier.activePlayableChunkCount << " / "
					  << frontier.requiredActiveChunkCount << std::endl;
		}

		if (shouldExpandPlayableBounds())
		{
			expandWorldOneRing();
		}
	}

	bool shouldExpandPlayableBounds() const
	{
		if (usesClassicStreaming())
		{
			return false;
		}

		size_t activePlayableChunkCount = static_cast<size_t>(frontier.activePlayableChunkCount);
		size_t requiredChunkCount = static_cast<size_t>(frontier.requiredActiveChunkCount);
		if (requiredChunkCount == 0)
		{
			return false;
		}
		if (activePlayableChunkCount < requiredChunkCount)
		{
			return false;
		}
		return true;
	}

	void expandWorldOneRing()
	{
		if (usesClassicStreaming())
		{
			return;
		}

		ChunkBounds previousGenerated = frontier.generatedBounds;
		frontier.playableBounds = frontier.playableBounds.expanded(1);
		frontier.generatedBounds = frontier.playableBounds.expanded(frontier.paddingChunks);
		updateExpansionProgress();

		for (int cx = frontier.generatedBounds.minChunkX; cx < frontier.generatedBounds.maxChunkXExclusive; cx++)
		{
			for (int cz = frontier.generatedBounds.minChunkZ; cz < frontier.generatedBounds.maxChunkZExclusive; cz++)
			{
				if (previousGenerated.containsChunk(cx, cz))
				{
					continue;
				}
				scheduleChunkGeneration(cx, cz);
			}
		}

		broadcastReliable(encodeWorldFrontier(frontier));
		std::cout << "Expanded playable bounds to "
				  << frontier.playableBounds.widthChunks() << "x"
				  << frontier.playableBounds.depthChunks() << " chunks" << std::endl;
	}

	void scheduleBounds(const ChunkBounds &bounds)
	{
		for (int cx = bounds.minChunkX; cx < bounds.maxChunkXExclusive; cx++)
		{
			for (int cz = bounds.minChunkZ; cz < bounds.maxChunkZExclusive; cz++)
			{
				scheduleChunkGeneration(cx, cz);
			}
		}
	}

	void scheduleChunkGeneration(int cx, int cz)
	{
		int64_t key = chunkKey(cx, cz);
		if (worldChunks.find(key) != worldChunks.end())
		{
			return;
		}

		std::lock_guard<std::mutex> lock(taskMutex);
		if (scheduledChunkKeys.find(key) != scheduledChunkKeys.end())
		{
			return;
		}
		scheduledChunkKeys.insert(key);
		generationTasks.push_back(ChunkCoord{cx, cz});
		taskCv.notify_one();
	}

	void queueChunkForClient(ClientSession &session, int64_t key)
	{
		if (session.wantedChunks.find(key) == session.wantedChunks.end())
		{
			return;
		}
		if (session.loadedChunks.find(key) != session.loadedChunks.end())
		{
			return;
		}
		if (session.queuedChunks.find(key) != session.queuedChunks.end())
		{
			return;
		}
		session.sendQueue.push_back(key);
		session.queuedChunks.insert(key);
	}

	void sendQueuedChunks(ClientSession &session)
	{
		size_t sentCount = 0;
		size_t sendBudget = chunkSendBudgetForClient(session);

		while (sentCount < sendBudget && !session.sendQueue.empty())
		{
			int64_t key = session.sendQueue.front();
			session.sendQueue.pop_front();
			session.queuedChunks.erase(key);

			if (session.wantedChunks.find(key) == session.wantedChunks.end())
			{
				continue;
			}

			auto worldIt = worldChunks.find(key);
			if (worldIt == worldChunks.end())
			{
				continue;
			}

			std::vector<uint8_t> payload = encodeChunkSnapshot(worldIt->second);
			sendReliable(session.peer, payload);
			profileSnapshotCount++;
			profileSnapshotPayloadBytes += payload.size();
			profileSnapshotSectionCount += worldIt->second.nonEmptySectionCount();
			profileSnapshotRawBytes +=
				sizeof(PacketType) +
				sizeof(worldIt->second.chunkX) +
				sizeof(worldIt->second.chunkZ) +
				sizeof(worldIt->second.revision) +
				sizeof(worldIt->second.blocks);
			session.loadedChunks.insert(key);
			sentCount++;
		}
	}

	size_t chunkSendBudgetForClient(const ClientSession &session)
	{
		if (!usesClassicStreaming())
		{
			return DEFAULT_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK;
		}

		size_t queueSize = session.sendQueue.size();
		size_t budget = CLASSIC_MIN_CHUNK_SENDS_PER_CLIENT_PER_TICK;
		if (queueSize > 64)
		{
			budget = CLASSIC_MID_CHUNK_SENDS_PER_CLIENT_PER_TICK;
		}
		if (queueSize > 256)
		{
			budget = CLASSIC_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK;
		}
		return budget;
	}

	void sendReliable(ENetPeer *peer, const std::vector<uint8_t> &payload)
	{
		ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(peer, WORLD_CHANNEL_RELIABLE, packet);
	}

	void broadcastReliable(const std::vector<uint8_t> &payload)
	{
		for (auto &entry : clients)
		{
			sendReliable(entry.first, payload);
		}
	}
};

WorldServer::WorldServer(uint16_t port,
						 std::unique_ptr<IChunkGenerator> generator,
						 WorldGenerationMode generationMode)
{
	m_impl = new Impl(port, std::move(generator), generationMode);
}

WorldServer::~WorldServer()
{
	delete m_impl;
}

bool WorldServer::start()
{
	return m_impl->start();
}

int WorldServer::run()
{
	return m_impl->run();
}

void WorldServer::stop()
{
	m_impl->stop();
}
