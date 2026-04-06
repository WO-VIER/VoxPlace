#include <WorldServer.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScoped
#define ZoneScopedN(name)
#define FrameMark
#endif

#include <ChunkPalette.h>
#include <PasswordHasher.h>
#include <Player.h>
#include <PlayerSessionData.h>
#include <PlayerTable.h>
#include <PlayerUsername.h>
#include <WorldTable.h>

#include <enet/enet.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <csignal>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	constexpr size_t WORLD_CHANNEL_RELIABLE = 0;
	constexpr int SERVER_TICK_MS = 50;
	constexpr size_t DEFAULT_MAX_INTEGRATED_CHUNKS_PER_TICK = 4;
	constexpr size_t DEFAULT_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK = 100;
	constexpr size_t MAX_PENDING_CHUNK_SNAPSHOTS_PER_CLIENT = 512;
	constexpr size_t DEFAULT_MAX_CHUNK_SAVES_PER_TICK = 8;
	constexpr size_t DEFAULT_MAX_CHUNK_UNLOADS_PER_TICK = 8;
	constexpr size_t CLASSIC_MIN_INTEGRATED_CHUNKS_PER_TICK = 8;
	constexpr size_t CLASSIC_MID_INTEGRATED_CHUNKS_PER_TICK = 16;
	constexpr size_t CLASSIC_MAX_INTEGRATED_CHUNKS_PER_TICK = 64;
	constexpr size_t CLASSIC_MIN_CHUNK_SENDS_PER_CLIENT_PER_TICK = 32;
	constexpr size_t CLASSIC_MID_CHUNK_SENDS_PER_CLIENT_PER_TICK = 128;
	constexpr size_t CLASSIC_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK = 256;
	constexpr size_t CLASSIC_MAX_CHUNK_UNLOADS_PER_TICK = 32;
	constexpr int INITIAL_PLAYABLE_RADIUS = 1;
	constexpr int INITIAL_PADDING_CHUNKS = 0;
	volatile std::sig_atomic_t gWorldServerSignalStopRequested = 0;

		size_t computeWorkerCount(size_t requestedWorkerCount)
		{
			if (requestedWorkerCount > 0)
			{
				return requestedWorkerCount;
			}

			unsigned int reported = std::thread::hardware_concurrency();
			printf("Threads %d\n", reported);
			if (reported == 0)
			{
				return 1;
			}

			// Politique serveur dédiée:
			// - 1 thread logique réservé au main thread (réseau + tick + intégration)
			// - 1 thread logique réservé au save worker SQLite
			// - tout le reste est alloué au pool de génération
			//
			// Cela pousse beaucoup plus fort qu'une heuristique desktop partagée
			// client+serveur, tout en gardant explicitement la place pour le thread
			// autoritaire et la persistance asynchrone.
			if (reported <= 2)
			{
				return 1;
			}
			return static_cast<size_t>(reported - 2);
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

	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}
}

struct WorldServer::Impl
{
	struct ClientSession
	{
		struct ClientChunkStreamState
		{
			std::unordered_set<int64_t> wantedChunks;
			std::unordered_set<int64_t> loadedChunks;
			std::unordered_set<int64_t> queuedChunks;
			std::unordered_set<uint64_t> pendingPacketIds;
			std::deque<int64_t> sendQueue;
		};

		struct ClientPlayerContext
		{
			Player player;
			PlayerSessionData playerSession;
			std::string usernameKey;
		};

		ENetPeer *peer = nullptr;
		ClientChunkStreamState chunkStream;
		ClientPlayerContext playerContext;
	};

	struct ReadyChunk
	{
		VoxelChunkData chunk;
		bool loadedFromStorage = false;
	};

	struct SaveBatchJob
	{
		std::vector<VoxelChunkData> chunks;
	};

	struct PendingChunkPacket
	{
		ENetPeer *peer = nullptr;
		uint64_t packetId = 0;
	};

		uint16_t port = 0;
			std::string playerDatabasePath;
			std::string worldDatabasePath;
			bool persistGeneratedChunks = true;
		ServerEnvironmentOptions environmentOptions;
	bool enetInitialized = false;
	ENetHost *host = nullptr;
	std::unique_ptr<IChunkGenerator> generator;
		WorldGenerationMode generationMode = WorldGenerationMode::ActivityFrontier;
		PlayerTable playerTable;
		WorldTable worldTable;
	PasswordHasher passwordHasher;

	WorldFrontier frontier;
	std::unordered_map<int64_t, VoxelChunkData> worldChunks;
	std::unordered_set<int64_t> activeChunkKeys;
	std::unordered_map<ENetPeer *, ClientSession> clients;
	std::unordered_map<ENetPacket *, PendingChunkPacket> pendingChunkPackets;
	std::unordered_map<std::string, uint64_t> activeUsernames;
		std::unordered_set<int64_t> dirtyChunkKeys;
		std::unordered_set<int64_t> queuedDirtyChunkKeys;
		std::deque<int64_t> dirtyChunkQueue;
			mutable std::mutex persistedChunkKeysMutex;
			std::unordered_set<int64_t> persistedChunkKeys;
			mutable std::mutex saveMutex;
		std::condition_variable saveCv;
		std::deque<SaveBatchJob> saveJobs;
		std::unordered_map<int64_t, size_t> pendingSaveChunkCounts;
		std::thread saveWorker;
		bool saveStopRequested = false;

	std::atomic<bool> running = false;
	std::mutex taskMutex;
	std::condition_variable taskCv;
	std::deque<ChunkCoord> generationTasks;
	std::unordered_set<int64_t> scheduledChunkKeys;
	std::mutex readyMutex;
	std::deque<ReadyChunk> readyChunks;
	std::vector<std::thread> workers;
	size_t workerCount = 0;
	bool profileWorkers = false;
	std::chrono::steady_clock::time_point profileWindowStart;
	std::atomic<size_t> profileReadyChunks = 0;
	std::atomic<size_t> profileLoadedChunks = 0;
	std::atomic<size_t> profileGeneratedFreshChunks = 0;
	std::atomic<size_t> profileLoadErrorChunks = 0;
	size_t profileIntegratedChunks = 0;
	size_t profileIntegratedLoadedChunks = 0;
	size_t profileIntegratedGeneratedChunks = 0;
	size_t profileQueuedForSendChunks = 0;
	size_t profileMarkedDirtyChunks = 0;
	size_t profileTickCount = 0;
	size_t profileAccumGenerationTasks = 0;
	size_t profileAccumReadyChunks = 0;
	size_t profileMaxGenerationTasks = 0;
	size_t profileMaxReadyChunks = 0;
	size_t profileSnapshotCount = 0;
	size_t profileSnapshotPayloadBytes = 0;
	size_t profileSnapshotRawBytes = 0;
	size_t profileSnapshotSectionCount = 0;
	uint64_t nextChunkSnapshotPacketId = 1;
	std::atomic<size_t> profileSaveBatchCount = 0;
	std::atomic<size_t> profileSavedChunkCount = 0;
	size_t profileUnloadedChunks = 0;

			Impl(uint16_t listenPort,
				 std::unique_ptr<IChunkGenerator> worldGenerator,
				 WorldGenerationMode selectedGenerationMode,
				 std::string selectedPlayerDatabasePath,
				 std::string selectedWorldDatabasePath,
				 bool shouldPersistGeneratedChunks,
				 ServerEnvironmentOptions selectedEnvironmentOptions)
				: port(listenPort),
				  playerDatabasePath(std::move(selectedPlayerDatabasePath)),
				  worldDatabasePath(std::move(selectedWorldDatabasePath)),
				  persistGeneratedChunks(shouldPersistGeneratedChunks),
			  environmentOptions(selectedEnvironmentOptions),
		  generator(std::move(worldGenerator)),
		  generationMode(selectedGenerationMode),
		  profileWorkers(environmentOptions.profileWorkersEnabled)
	{
		frontier.playableBounds = makeSquareBounds(INITIAL_PLAYABLE_RADIUS);
		frontier.paddingChunks = INITIAL_PADDING_CHUNKS;
		frontier.generatedBounds = frontier.playableBounds.expanded(frontier.paddingChunks);
		frontier.mode = generationMode;
		updateExpansionProgress();
	}

	~Impl()
	{
		stop();
	}

		bool start()
		{
				if (!playerTable.open(playerDatabasePath))
				{
				std::cerr << "Failed to open player database: "
						  << playerTable.lastError() << std::endl;
				return false;
			}
				if (!worldTable.open(worldDatabasePath, worldGenerationModeName(generationMode)))
				{
					std::cerr << "Failed to open world database: "
							  << worldTable.lastErrorCopy() << std::endl;
					playerTable.close();
					return false;
				}
				if (!persistGeneratedChunks)
				{
					if (!loadPersistedChunkKeys())
					{
						std::cerr << "Failed to preload modified chunk keys: "
								  << worldTable.lastErrorCopy() << std::endl;
						worldTable.close();
						playerTable.close();
						return false;
					}
				}

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
		workerCount = computeWorkerCount(environmentOptions.requestedWorkerCount);
		for (size_t i = 0; i < workerCount; i++)
		{
			workers.emplace_back(&Impl::workerLoop, this);
		}
		saveStopRequested = false;
		saveWorker = std::thread(&Impl::saveWorkerLoop, this);
		profileWindowStart = std::chrono::steady_clock::now();

			std::cout << "WorldServer listening on port " << port
					  << " with " << workerCount << " generation worker(s)"
					  << " in " << worldGenerationModeName(generationMode)
					  << " mode" << std::endl;
				std::cout << "World DB path: " << worldDatabasePath << std::endl;
			if (!persistGeneratedChunks)
			{
				std::cout << "Modified-only world cache contains "
						  << persistedChunkKeyCount()
						  << " persisted chunk(s)" << std::endl;
			}
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
			integrateReadyChunks((std::numeric_limits<size_t>::max)());
			flushDirtyChunks((std::numeric_limits<size_t>::max)());
			stopSaveWorker();
			saveAllAuthenticatedPlayers();
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
		integrateReadyChunks((std::numeric_limits<size_t>::max)());
		flushDirtyChunks((std::numeric_limits<size_t>::max)());
		stopSaveWorker();
		saveAllAuthenticatedPlayers();
		cleanupNetwork();
	}

	void cleanupNetwork()
	{
		if (host != nullptr)
		{
			enet_host_destroy(host);
			host = nullptr;
		}
		pendingChunkPackets.clear();
		if (enetInitialized)
		{
			enet_deinitialize();
			enetInitialized = false;
		}
		worldTable.close();
		playerTable.close();
	}

	int run()
	{
		using clock = std::chrono::steady_clock;
		auto nextTick = clock::now();

		while (running)
		{
			if (isWorldServerSignalStopRequested())
			{
				std::cout << "Stop signal received, shutting down server cleanly" << std::endl;
				stop();
				break;
			}

			serviceNetwork(1);
			auto now = clock::now();
			if (now >= nextTick)
			{
				tick();
				nextTick = now + std::chrono::milliseconds(SERVER_TICK_MS);
			}

			FrameMark;
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

					ZoneScopedN("Worker Generate Chunk");
					ReadyChunk readyChunk;
					readyChunk.chunk = VoxelChunkData(coord.x, coord.z);
					int64_t key = chunkKey(coord.x, coord.z);
					bool loadedFromStorage = false;
					bool shouldTryStorageLoad = true;
					if (!persistGeneratedChunks)
					{
						shouldTryStorageLoad = isChunkPersistedOnDisk(key);
					}

					if (shouldTryStorageLoad)
					{
						std::string loadError;
						WorldTableLoadChunkResult loadResult;
						{
							ZoneScopedN("SQLite: Load Chunk");
							loadResult = worldTable.loadChunkResult(
								coord.x,
								coord.z,
								readyChunk.chunk,
								&loadError);
						}

						if (loadResult == WorldTableLoadChunkResult::Loaded)
						{
							loadedFromStorage = true;
						}
						else if (loadResult == WorldTableLoadChunkResult::Error)
						{
							std::cerr << "Failed to load world chunk " << coord.x << ","
									  << coord.z << " from storage: "
									  << loadError << std::endl;
							profileLoadErrorChunks.fetch_add(1, std::memory_order_relaxed);
						}
					}
					readyChunk.loadedFromStorage = loadedFromStorage;

					if (loadedFromStorage)
					{
						profileLoadedChunks.fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						{
							ZoneScopedN("CPU: FastNoise Gen Terrain");
							generator->fillChunk(readyChunk.chunk);
						}
					profileGeneratedFreshChunks.fetch_add(1, std::memory_order_relaxed);
				}

				std::lock_guard<std::mutex> readyLock(readyMutex);
				readyChunks.push_back(std::move(readyChunk));
				profileReadyChunks.fetch_add(1, std::memory_order_relaxed);
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
		ZoneScopedN("Server Tick");
		integrateReadyChunks(integratedChunksBudgetForTick());
		for (auto &entry : clients)
		{
			sendQueuedChunks(entry.second);
		}
		flushDirtyChunks(DEFAULT_MAX_CHUNK_SAVES_PER_TICK);
		unloadColdChunks(unloadChunksBudgetForTick());
		enet_host_flush(host);
		logWorkerProfileWindowIfNeeded();
	}

	bool isChunkWantedByAnyClient(int64_t key) const
	{
		for (const auto &entry : clients)
		{
			const ClientSession &session = entry.second;
			if (session.chunkStream.wantedChunks.find(key) != session.chunkStream.wantedChunks.end())
			{
				return true;
			}
		}
		return false;
	}

		bool canUnloadChunkNow(int64_t key) const
		{
			if (activeChunkKeys.find(key) != activeChunkKeys.end())
		{
			return false;
		}
		if (dirtyChunkKeys.find(key) != dirtyChunkKeys.end())
		{
			return false;
		}
			if (queuedDirtyChunkKeys.find(key) != queuedDirtyChunkKeys.end())
			{
				return false;
			}
			if (isChunkPendingSave(key))
			{
				return false;
			}
			if (isChunkWantedByAnyClient(key))
			{
				return false;
		}
		return true;
	}

	size_t unloadChunksBudgetForTick() const
	{
		if (!usesClassicStreaming())
		{
			return DEFAULT_MAX_CHUNK_UNLOADS_PER_TICK;
		}
		return CLASSIC_MAX_CHUNK_UNLOADS_PER_TICK;
	}

	void unloadColdChunks(size_t maxCount)
	{
		if (maxCount == 0 || worldChunks.empty())
		{
			return;
		}

		size_t unloadedCount = 0;
		for (auto it = worldChunks.begin();
			 it != worldChunks.end() && unloadedCount < maxCount;)
		{
			int64_t key = it->first;
			if (!canUnloadChunkNow(key))
			{
				++it;
				continue;
			}

			it = worldChunks.erase(it);
			unloadedCount++;
		}

		profileUnloadedChunks += unloadedCount;
	}

	void integrateReadyChunks(size_t maxCount)
	{
		size_t integratedCount = 0;

		while (integratedCount < maxCount)
		{
				ReadyChunk readyChunk;
				{
					std::lock_guard<std::mutex> readyLock(readyMutex);
					if (readyChunks.empty())
					{
						break;
					}
					readyChunk = std::move(readyChunks.front());
					readyChunks.pop_front();
				}
				VoxelChunkData chunk = std::move(readyChunk.chunk);

					int64_t key = chunkKey(chunk.chunkX, chunk.chunkZ);
					worldChunks[key] = std::move(chunk);
					if (readyChunk.loadedFromStorage)
					{
						profileIntegratedLoadedChunks++;
					}
					else
					{
						profileIntegratedGeneratedChunks++;
					}
					if (!readyChunk.loadedFromStorage && persistGeneratedChunks)
					{
						markChunkDirty(key);
						profileMarkedDirtyChunks++;
					}
				{
					std::lock_guard<std::mutex> taskLock(taskMutex);
					scheduledChunkKeys.erase(key);
			}
			for (auto &entry : clients)
			{
				ClientSession &session = entry.second;
				if (session.chunkStream.wantedChunks.find(key) != session.chunkStream.wantedChunks.end())
				{
					queueChunkForClient(session.chunkStream, key);
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

		size_t readyWindow = profileReadyChunks.exchange(0, std::memory_order_relaxed);
		size_t loadedWindow = profileLoadedChunks.exchange(0, std::memory_order_relaxed);
		size_t generatedFreshWindow = profileGeneratedFreshChunks.exchange(0, std::memory_order_relaxed);
		size_t loadErrorsWindow = profileLoadErrorChunks.exchange(0, std::memory_order_relaxed);
		size_t saveBatchWindow = profileSaveBatchCount.exchange(0, std::memory_order_relaxed);
		size_t savedChunksWindow = profileSavedChunkCount.exchange(0, std::memory_order_relaxed);

		size_t queuedSendCount = 0;
		for (const auto &entry : clients)
		{
			queuedSendCount += entry.second.chunkStream.sendQueue.size();
		}

		size_t saveQueueJobsNow = 0;
		{
			std::lock_guard<std::mutex> lock(saveMutex);
			saveQueueJobsNow = saveJobs.size();
		}

		std::cout << "[server-profile] workers=" << workerCount
				  << " mode=" << worldGenerationModeName(generationMode)
				  << " clients=" << clients.size()
				  << " world_chunks=" << worldChunks.size()
				  << " ready_window=" << readyWindow
				  << " loaded_window=" << loadedWindow
				  << " generated_fresh_window=" << generatedFreshWindow
				  << " load_errors_window=" << loadErrorsWindow
				  << " integrated_window=" << profileIntegratedChunks
				  << " integrated_loaded_window=" << profileIntegratedLoadedChunks
				  << " integrated_generated_window=" << profileIntegratedGeneratedChunks
				  << " unloaded_window=" << profileUnloadedChunks
				  << " queued_for_send_window=" << profileQueuedForSendChunks
				  << " send_queue_now=" << queuedSendCount
				  << " dirty_marked_window=" << profileMarkedDirtyChunks
				  << " dirty_queue_now=" << dirtyChunkQueue.size()
				  << " save_queue_jobs_now=" << saveQueueJobsNow
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

		double saveAvgChunks = 0.0;
		if (saveBatchWindow > 0)
		{
			saveAvgChunks =
				static_cast<double>(savedChunksWindow) /
				static_cast<double>(saveBatchWindow);
		}
		std::cout << " saved_chunks_window=" << savedChunksWindow
				  << " save_batches_window=" << saveBatchWindow
				  << " save_avg_chunks=" << saveAvgChunks;

		std::cout
			<< std::endl;

		profileWindowStart = now;
		profileIntegratedChunks = 0;
		profileIntegratedLoadedChunks = 0;
		profileIntegratedGeneratedChunks = 0;
		profileUnloadedChunks = 0;
		profileQueuedForSendChunks = 0;
		profileMarkedDirtyChunks = 0;
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
		session.playerContext.playerSession.lastSeenAtMs = systemNowMs();
		clients[peer] = std::move(session);
		std::cout << "Client connected" << std::endl;
	}

	void handleDisconnect(ENetPeer *peer)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt != clients.end())
		{
			savePlayerForSession(sessionIt->second);
			if (!sessionIt->second.playerContext.usernameKey.empty())
			{
				activeUsernames.erase(sessionIt->second.playerContext.usernameKey);
			}
			clients.erase(sessionIt);
		}
		std::cout << "Client disconnected" << std::endl;
	}

	void sendPlayerState(ClientSession &session)
	{
		if (!session.playerContext.playerSession.authenticated)
		{
			return;
		}

		PlayerStateMessage message;
		message.playerId = session.playerContext.player.profile.playerId;
		message.positionX = session.playerContext.player.state.position.x;
		message.positionY = session.playerContext.player.state.position.y;
		message.positionZ = session.playerContext.player.state.position.z;
		message.blockActionReadyAtMs = session.playerContext.player.state.blockActionReadyAtMs;
		message.serverNowMs = systemNowMs();
		sendReliable(session.peer, encodePlayerState(message));
	}

	bool savePlayerForSession(ClientSession &session)
	{
		if (!session.playerContext.playerSession.authenticated)
		{
			return true;
		}
		if (!playerTable.isOpen())
		{
			return false;
		}
		if (!playerTable.savePlayer(session.playerContext.player))
		{
			std::cerr << "Failed to save player "
					  << session.playerContext.player.profile.username
					  << ": " << playerTable.lastError() << std::endl;
			return false;
		}
		return true;
	}

	void saveAllAuthenticatedPlayers()
	{
		for (auto &entry : clients)
		{
			savePlayerForSession(entry.second);
		}
	}

	void markChunkDirty(int64_t key)
	{
		dirtyChunkKeys.insert(key);
		if (queuedDirtyChunkKeys.insert(key).second)
		{
			dirtyChunkQueue.push_back(key);
		}
	}

		void enqueueSaveBatch(std::vector<VoxelChunkData> &&chunks)
		{
			if (chunks.empty())
			{
			return;
		}

			{
				std::lock_guard<std::mutex> lock(saveMutex);
				for (const VoxelChunkData &chunk : chunks)
				{
					int64_t key = chunkKey(chunk.chunkX, chunk.chunkZ);
					pendingSaveChunkCounts[key]++;
				}
				saveJobs.push_back(SaveBatchJob{std::move(chunks)});
			}
			saveCv.notify_one();
		}

	void saveWorkerLoop()
	{
		while (true)
		{
			SaveBatchJob job;
			{
				std::unique_lock<std::mutex> lock(saveMutex);
				saveCv.wait(lock, [&]()
							{ return saveStopRequested || !saveJobs.empty(); });
				if (saveJobs.empty())
				{
					if (saveStopRequested)
					{
						return;
					}
					continue;
				}

				job = std::move(saveJobs.front());
				saveJobs.pop_front();
			}

			ZoneScopedN("SQLite Save Worker");
				{
					ZoneScopedN("SQLite: Save Chunk Batch");
					if (!worldTable.saveChunksBatch(job.chunks))
					{
						std::string saveError = worldTable.lastErrorCopy();
						std::cerr << "Failed to save world chunk batch: "
								  << saveError << std::endl;
						{
							std::lock_guard<std::mutex> lock(saveMutex);
							saveJobs.push_front(std::move(job));
					}
					saveCv.notify_one();
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
						continue;
					}
					{
						std::lock_guard<std::mutex> lock(saveMutex);
						for (const VoxelChunkData &chunk : job.chunks)
						{
							int64_t key = chunkKey(chunk.chunkX, chunk.chunkZ);
							auto pendingIt = pendingSaveChunkCounts.find(key);
							if (pendingIt == pendingSaveChunkCounts.end())
							{
								continue;
							}
							if (pendingIt->second <= 1)
							{
								pendingSaveChunkCounts.erase(pendingIt);
							}
							else
							{
								pendingIt->second--;
							}
						}
					}
					if (!persistGeneratedChunks)
					{
						for (const VoxelChunkData &chunk : job.chunks)
						{
							rememberPersistedChunkKey(chunkKey(chunk.chunkX, chunk.chunkZ));
						}
					}
					profileSaveBatchCount.fetch_add(1, std::memory_order_relaxed);
					profileSavedChunkCount.fetch_add(job.chunks.size(), std::memory_order_relaxed);
				}
		}
	}

	void stopSaveWorker()
	{
		{
			std::lock_guard<std::mutex> lock(saveMutex);
			saveStopRequested = true;
		}
		saveCv.notify_all();
		if (saveWorker.joinable())
		{
			saveWorker.join();
		}
	}

		void flushDirtyChunks(size_t maxCount)
		{
		ZoneScopedN("SQLite: Flush Dirty Chunks");
		std::vector<int64_t> attemptedKeys;
		std::vector<VoxelChunkData> chunksToSave;
		size_t reserveCount = dirtyChunkQueue.size();
		if (maxCount < reserveCount)
		{
			reserveCount = maxCount;
		}
		attemptedKeys.reserve(reserveCount);
		chunksToSave.reserve(reserveCount);

		{
			ZoneScopedN("SQLite: Collect Dirty Chunk Batch");
			while (chunksToSave.size() < maxCount && !dirtyChunkQueue.empty())
			{
				int64_t key = dirtyChunkQueue.front();
				dirtyChunkQueue.pop_front();
				queuedDirtyChunkKeys.erase(key);

				if (dirtyChunkKeys.find(key) == dirtyChunkKeys.end())
				{
					continue;
				}

				auto worldIt = worldChunks.find(key);
				if (worldIt == worldChunks.end())
				{
					dirtyChunkKeys.erase(key);
					continue;
				}

				attemptedKeys.push_back(key);
				chunksToSave.push_back(worldIt->second);
			}
		}

		if (chunksToSave.empty())
		{
			return;
		}

		{
			ZoneScopedN("SQLite: Queue Save Chunk Batch");
			enqueueSaveBatch(std::move(chunksToSave));
		}

			for (int64_t key : attemptedKeys)
			{
				dirtyChunkKeys.erase(key);
			}
		}

		bool loadPersistedChunkKeys()
		{
			std::vector<int64_t> storedKeys;
			if (!worldTable.loadAllChunkKeys(storedKeys))
			{
				return false;
			}

			std::lock_guard<std::mutex> lock(persistedChunkKeysMutex);
			persistedChunkKeys.clear();
			persistedChunkKeys.reserve(storedKeys.size());
			for (int64_t key : storedKeys)
			{
				persistedChunkKeys.insert(key);
			}
			return true;
		}

		size_t persistedChunkKeyCount() const
		{
			std::lock_guard<std::mutex> lock(persistedChunkKeysMutex);
			return persistedChunkKeys.size();
		}

		bool isChunkPersistedOnDisk(int64_t key) const
		{
			std::lock_guard<std::mutex> lock(persistedChunkKeysMutex);
			if (persistedChunkKeys.find(key) != persistedChunkKeys.end())
			{
				return true;
			}
			return false;
		}

		void rememberPersistedChunkKey(int64_t key)
		{
			std::lock_guard<std::mutex> lock(persistedChunkKeysMutex);
			persistedChunkKeys.insert(key);
		}

		bool isChunkPendingSave(int64_t key) const
		{
			std::lock_guard<std::mutex> lock(saveMutex);
			auto pendingIt = pendingSaveChunkCounts.find(key);
			if (pendingIt == pendingSaveChunkCounts.end())
			{
				return false;
			}
			if (pendingIt->second == 0)
			{
				return false;
			}
			return true;
		}

	void handleLoginRequest(ENetPeer *peer, const LoginRequestMessage &request)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}

		ClientSession &session = sessionIt->second;
		session.playerContext.playerSession.lastSeenAtMs = systemNowMs();
		if (session.playerContext.playerSession.authenticated)
		{
			return;
		}

		std::string trimmedUsername = trimPlayerUsername(playerUsernameFromBuffer(request.username));
		PlayerUsernameValidationError usernameError = validatePlayerUsername(trimmedUsername);

		LoginResponseMessage response;
		response.serverNowMs = systemNowMs();
		if (usernameError != PlayerUsernameValidationError::None)
		{
			response.status = LoginStatus::InvalidUsername;
			sendReliable(peer, encodeLoginResponse(response));
			return;
		}

		if (activeUsernames.find(trimmedUsername) != activeUsernames.end())
		{
			response.status = LoginStatus::UsernameAlreadyInUse;
			copyPlayerUsernameToBuffer(trimmedUsername, response.username);
			sendReliable(peer, encodeLoginResponse(response));
			return;
		}

		std::string password = std::string(request.password);
		if (password.empty())
		{
			response.status = LoginStatus::InvalidCredentials;
			sendReliable(peer, encodeLoginResponse(response));
			return;
		}
		std::string passwordHashForNewPlayer;
		if (!passwordHasher.hashPassword(password, passwordHashForNewPlayer))
		{
			std::cerr << "Failed to hash password for "
					  << trimmedUsername
					  << ": " << passwordHasher.lastError() << std::endl;
			response.status = LoginStatus::InvalidCredentials;
			sendReliable(peer, encodeLoginResponse(response));
			return;
		}

		bool createdPlayer = false;
		Player loadedPlayer;
		std::string storedPasswordHash;
		if (!playerTable.loadOrCreatePlayer(
				trimmedUsername,
				passwordHashForNewPlayer,
				loadedPlayer,
				storedPasswordHash,
				createdPlayer))
		{
			std::cerr << "Failed to load/create player "
					  << trimmedUsername
					  << ": " << playerTable.lastError() << std::endl;
			response.status = LoginStatus::InvalidUsername;
			sendReliable(peer, encodeLoginResponse(response));
			return;
		}

		session.playerContext.player = loadedPlayer;
		if (!createdPlayer)
		{
			if (!storedPasswordHash.empty())
			{
				if (password.empty() ||
					!passwordHasher.verifyPassword(password, storedPasswordHash))
				{
					response.status = LoginStatus::InvalidCredentials;
					sendReliable(peer, encodeLoginResponse(response));
					return;
				}
			}
			else
			{
				if (!playerTable.updatePasswordHash(
						session.playerContext.player.profile.playerId,
						passwordHashForNewPlayer))
				{
					std::cerr << "Failed to set password hash for "
							  << trimmedUsername
							  << ": " << playerTable.lastError() << std::endl;
					response.status = LoginStatus::InvalidCredentials;
					sendReliable(peer, encodeLoginResponse(response));
					return;
				}
			}
		}

		session.playerContext.playerSession.playerId = session.playerContext.player.profile.playerId;
		session.playerContext.playerSession.authenticated = true;
		session.playerContext.usernameKey = trimmedUsername;
		activeUsernames[trimmedUsername] = session.playerContext.player.profile.playerId;

		response.status = LoginStatus::Accepted;
		response.playerId = session.playerContext.player.profile.playerId;
		copyPlayerUsernameToBuffer(trimmedUsername, response.username);
		response.skinId = session.playerContext.player.profile.skinId;
		response.positionX = session.playerContext.player.state.position.x;
		response.positionY = session.playerContext.player.state.position.y;
		response.positionZ = session.playerContext.player.state.position.z;
		response.blockActionReadyAtMs = session.playerContext.player.state.blockActionReadyAtMs;
		sendReliable(peer, encodeLoginResponse(response));
		sendReliable(peer, encodeWorldFrontier(frontier));

		std::cout << "Player authenticated: " << trimmedUsername
				  << " id=" << session.playerContext.player.profile.playerId;
		if (createdPlayer)
		{
			std::cout << " (created)";
		}
		else
		{
			std::cout << " (loaded)";
		}
		std::cout << std::endl;
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
			return;
		}

		if (type == PacketType::LoginRequest)
		{
			LoginRequestMessage loginRequest;
			if (!decodeLoginRequest(data, size, loginRequest))
			{
				return;
			}
			handleLoginRequest(peer, loginRequest);
			return;
		}

		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}
		sessionIt->second.playerContext.playerSession.lastSeenAtMs = systemNowMs();
		if (!sessionIt->second.playerContext.playerSession.authenticated)
		{
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
			handleBlockAction(peer, request);
			return;
		}

		if (type == PacketType::PlayerMoveUpdate)
		{
			PlayerMoveUpdateMessage movement;
			if (!decodePlayerMoveUpdate(data, size, movement))
			{
				return;
			}
			handlePlayerMoveUpdate(peer, movement);
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
		session.chunkStream.wantedChunks.insert(key);

		auto worldIt = worldChunks.find(key);
		if (worldIt != worldChunks.end())
		{
			queueChunkForClient(session.chunkStream, key);
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
		session.chunkStream.wantedChunks.erase(key);
		session.chunkStream.loadedChunks.erase(key);
		session.chunkStream.queuedChunks.erase(key);
	}

	void handleBlockAction(ENetPeer *peer, const BlockActionRequestMessage &request)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}

		ClientSession &session = sessionIt->second;
		auto rejectActionAndSync = [&]()
		{
			sendPlayerState(session);
		};

		uint64_t nowMs = systemNowMs();
		if (nowMs < session.playerContext.player.state.blockActionReadyAtMs)
		{
			rejectActionAndSync();
			return;
		}
		if (request.worldY < 0 || request.worldY >= CHUNK_SIZE_Y)
		{
			rejectActionAndSync();
			return;
		}
		if (!usesClassicStreaming() &&
			!frontier.playableBounds.containsWorldBlock(request.worldX, request.worldZ))
		{
			rejectActionAndSync();
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
			rejectActionAndSync();
			return;
		}

		uint32_t finalColor = 0;
		if (request.action == BlockActionType::Place)
		{
			finalColor = playerPaletteColor(request.paletteIndex);
		}

		if (!worldIt->second.setBlockRaw(lx, request.worldY, lz, finalColor))
		{
			rejectActionAndSync();
			return;
		}

		markChunkDirty(key);
		session.playerContext.player.state.blockActionReadyAtMs = nowMs + PLAYER_DEFAULT_BLOCK_ACTION_COOLDOWN_MS;

		BlockUpdateBroadcastMessage update;
		update.worldX = request.worldX;
		update.worldY = request.worldY;
		update.worldZ = request.worldZ;
		update.finalColor = finalColor;
		update.revision = worldIt->second.revision;
		broadcastReliable(encodeBlockUpdateBroadcast(update));
		sendPlayerState(session);

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

	void handlePlayerMoveUpdate(ENetPeer *peer, const PlayerMoveUpdateMessage &movement)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}

		ClientSession &session = sessionIt->second;
		if (!std::isfinite(movement.positionX) ||
			!std::isfinite(movement.positionY) ||
			!std::isfinite(movement.positionZ) ||
			!std::isfinite(movement.lookX) ||
			!std::isfinite(movement.lookY) ||
			!std::isfinite(movement.lookZ))
		{
			return;
		}

		session.playerContext.player.state.position = glm::vec3(
			movement.positionX,
			movement.positionY,
			movement.positionZ);
		session.playerContext.player.state.lookDirection = glm::vec3(
			movement.lookX,
			movement.lookY,
			movement.lookZ);
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

	void queueChunkForClient(ClientSession::ClientChunkStreamState &chunkStream, int64_t key)
	{
		if (chunkStream.wantedChunks.find(key) == chunkStream.wantedChunks.end())
		{
			return;
		}
		if (chunkStream.loadedChunks.find(key) != chunkStream.loadedChunks.end())
		{
			return;
		}
		if (chunkStream.queuedChunks.find(key) != chunkStream.queuedChunks.end())
		{
			return;
		}
		chunkStream.sendQueue.push_back(key);
		chunkStream.queuedChunks.insert(key);
		profileQueuedForSendChunks++;
	}

	void sendQueuedChunks(ClientSession &session)
	{
		size_t sentCount = 0;
		size_t sendBudget = chunkSendBudgetForClient(session.chunkStream);

		while (sentCount < sendBudget &&
			   pendingChunkPacketCountForClient(session.chunkStream) < MAX_PENDING_CHUNK_SNAPSHOTS_PER_CLIENT &&
			   !session.chunkStream.sendQueue.empty())
		{
			int64_t key = session.chunkStream.sendQueue.front();
			session.chunkStream.sendQueue.pop_front();
			session.chunkStream.queuedChunks.erase(key);

			if (session.chunkStream.wantedChunks.find(key) == session.chunkStream.wantedChunks.end())
			{
				continue;
			}

			auto worldIt = worldChunks.find(key);
			if (worldIt == worldChunks.end())
			{
				continue;
			}

			std::vector<uint8_t> payload = encodeChunkSnapshotNetwork(worldIt->second);
			if (!sendChunkSnapshot(session, payload))
			{
				session.chunkStream.sendQueue.push_front(key);
				session.chunkStream.queuedChunks.insert(key);
				break;
			}
			profileSnapshotCount++;
			profileSnapshotPayloadBytes += payload.size();
			profileSnapshotSectionCount += worldIt->second.nonEmptySectionCount();
			profileSnapshotRawBytes +=
				sizeof(PacketType) +
				sizeof(worldIt->second.chunkX) +
				sizeof(worldIt->second.chunkZ) +
				sizeof(worldIt->second.revision) +
				sizeof(worldIt->second.blocks);
			session.chunkStream.loadedChunks.insert(key);
			sentCount++;
		}
	}

	size_t chunkSendBudgetForClient(const ClientSession::ClientChunkStreamState &chunkStream)
	{
		if (!usesClassicStreaming())
		{
			return DEFAULT_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK;
		}

		size_t queueSize = chunkStream.sendQueue.size();
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

	size_t pendingChunkPacketCountForClient(const ClientSession::ClientChunkStreamState &chunkStream) const
	{
		return chunkStream.pendingPacketIds.size();
	}

	static void onChunkSnapshotPacketFreed(ENetPacket *packet)
	{
		if (packet == nullptr)
		{
			return;
		}
		Impl *server = static_cast<Impl *>(packet->userData);
		if (server == nullptr)
		{
			return;
		}
		server->handleChunkSnapshotPacketFreed(packet);
	}

	void handleChunkSnapshotPacketFreed(ENetPacket *packet)
	{
		auto pendingIt = pendingChunkPackets.find(packet);
		if (pendingIt == pendingChunkPackets.end())
		{
			return;
		}

		PendingChunkPacket pendingPacket = pendingIt->second;
		pendingChunkPackets.erase(pendingIt);

		auto sessionIt = clients.find(pendingPacket.peer);
		if (sessionIt == clients.end())
		{
			return;
		}
		sessionIt->second.chunkStream.pendingPacketIds.erase(pendingPacket.packetId);
	}

	bool sendChunkSnapshot(ClientSession &session, const std::vector<uint8_t> &payload)
	{
		ENetPacket *packet = enet_packet_create(
			payload.data(),
			payload.size(),
			ENET_PACKET_FLAG_RELIABLE);
		if (packet == nullptr)
		{
			return false;
		}

		uint64_t packetId = nextChunkSnapshotPacketId;
		nextChunkSnapshotPacketId++;
		if (nextChunkSnapshotPacketId == 0)
		{
			nextChunkSnapshotPacketId = 1;
		}

		packet->freeCallback = &Impl::onChunkSnapshotPacketFreed;
		packet->userData = this;

		PendingChunkPacket pendingPacket;
		pendingPacket.peer = session.peer;
		pendingPacket.packetId = packetId;
		pendingChunkPackets[packet] = pendingPacket;
		session.chunkStream.pendingPacketIds.insert(packetId);

		int sendResult = enet_peer_send(session.peer, WORLD_CHANNEL_RELIABLE, packet);
		if (sendResult != 0)
		{
			pendingChunkPackets.erase(packet);
			session.chunkStream.pendingPacketIds.erase(packetId);
			packet->freeCallback = nullptr;
			packet->userData = nullptr;
			enet_packet_destroy(packet);
			return false;
		}
		return true;
	}

	bool sendReliable(ENetPeer *peer, const std::vector<uint8_t> &payload)
	{
		ENetPacket *packet = enet_packet_create(
			payload.data(),
			payload.size(),
			ENET_PACKET_FLAG_RELIABLE);
		if (packet == nullptr)
		{
			return false;
		}

		int sendResult = enet_peer_send(peer, WORLD_CHANNEL_RELIABLE, packet);
		if (sendResult != 0)
		{
			enet_packet_destroy(packet);
			return false;
		}
		return true;
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
						 WorldGenerationMode generationMode,
						 std::string playerDatabasePath,
						 std::string worldDatabasePath,
						 bool persistGeneratedChunks,
						 ServerEnvironmentOptions environmentOptions)
{
	m_impl = new Impl(
		port,
		std::move(generator),
		generationMode,
		std::move(playerDatabasePath),
		std::move(worldDatabasePath),
		persistGeneratedChunks,
		environmentOptions);
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

void requestWorldServerSignalStop()
{
	gWorldServerSignalStopRequested = 1;
}

void resetWorldServerSignalStop()
{
	gWorldServerSignalStopRequested = 0;
}

bool isWorldServerSignalStopRequested()
{
	return gWorldServerSignalStopRequested != 0;
}
