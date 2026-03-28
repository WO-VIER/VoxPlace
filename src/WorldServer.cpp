#include <WorldServer.h>

#include <ChunkPalette.h>
#include <PasswordHasher.h>
#include <PlayerData.h>
#include <PlayerHotData.h>
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
	constexpr size_t DEFAULT_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK = 5;
	constexpr size_t DEFAULT_MAX_CHUNK_SAVES_PER_TICK = 1;
	constexpr size_t CLASSIC_MIN_INTEGRATED_CHUNKS_PER_TICK = 8;
	constexpr size_t CLASSIC_MID_INTEGRATED_CHUNKS_PER_TICK = 16;
	constexpr size_t CLASSIC_MAX_INTEGRATED_CHUNKS_PER_TICK = 32;
	constexpr size_t CLASSIC_MIN_CHUNK_SENDS_PER_CLIENT_PER_TICK = 8;
	constexpr size_t CLASSIC_MID_CHUNK_SENDS_PER_CLIENT_PER_TICK = 16;
	constexpr size_t CLASSIC_MAX_CHUNK_SENDS_PER_CLIENT_PER_TICK = 24;
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
		ENetPeer *peer = nullptr;
		std::unordered_set<int64_t> wantedChunks;
		std::unordered_set<int64_t> loadedChunks;
		std::unordered_set<int64_t> queuedChunks;
		std::deque<int64_t> sendQueue;
		PlayerData player;
		PlayerSessionData playerSession;
		std::string usernameKey;
	};

		uint16_t port = 0;
		std::string playerDatabasePath;
		std::string worldDatabasePath;
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
	std::unordered_map<std::string, uint64_t> activeUsernames;
	std::unordered_set<int64_t> dirtyChunkKeys;
	std::unordered_set<int64_t> queuedDirtyChunkKeys;
	std::deque<int64_t> dirtyChunkQueue;

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
			 WorldGenerationMode selectedGenerationMode,
			 std::string selectedPlayerDatabasePath,
			 std::string selectedWorldDatabasePath,
			 ServerEnvironmentOptions selectedEnvironmentOptions)
			: port(listenPort),
			  playerDatabasePath(std::move(selectedPlayerDatabasePath)),
			  worldDatabasePath(std::move(selectedWorldDatabasePath)),
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
						  << worldTable.lastError() << std::endl;
				playerTable.close();
				return false;
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
		profileWindowStart = std::chrono::steady_clock::now();

			std::cout << "WorldServer listening on port " << port
					  << " with " << workerCount << " generation worker(s)"
					  << " in " << worldGenerationModeName(generationMode)
					  << " mode" << std::endl;
			std::cout << "World DB path: " << worldDatabasePath << std::endl;
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
			flushDirtyChunks(std::numeric_limits<size_t>::max());
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
		flushDirtyChunks(std::numeric_limits<size_t>::max());
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
				bool loadedFromStorage = worldTable.loadChunk(coord.x, coord.z, chunk);
				if (!loadedFromStorage)
				{
					if (!worldTable.lastError().empty())
					{
						std::cerr << "Failed to load world chunk " << coord.x << ","
								  << coord.z << " from storage: "
								  << worldTable.lastError() << std::endl;
					}
					generator->fillChunk(chunk);
					if (!worldTable.saveChunk(chunk))
					{
						std::cerr << "Failed to persist generated world chunk "
								  << coord.x << "," << coord.z << ": "
								  << worldTable.lastError() << std::endl;
						markChunkDirty(chunkKey(coord.x, coord.z));
					}
				}

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
		flushDirtyChunks(DEFAULT_MAX_CHUNK_SAVES_PER_TICK);
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
		session.playerSession.lastSeenAtMs = systemNowMs();
		clients[peer] = std::move(session);
		std::cout << "Client connected" << std::endl;
	}

	void handleDisconnect(ENetPeer *peer)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt != clients.end())
		{
			savePlayerForSession(sessionIt->second);
			if (!sessionIt->second.usernameKey.empty())
			{
				activeUsernames.erase(sessionIt->second.usernameKey);
			}
			clients.erase(sessionIt);
		}
		std::cout << "Client disconnected" << std::endl;
	}

	void sendPlayerState(ClientSession &session)
	{
		if (!session.playerSession.authenticated)
		{
			return;
		}

		PlayerStateMessage message;
		message.playerId = session.player.cold.playerId;
		message.positionX = session.player.hot.position.x;
		message.positionY = session.player.hot.position.y;
		message.positionZ = session.player.hot.position.z;
		message.blockActionReadyAtMs = session.player.hot.blockActionReadyAtMs;
		message.serverNowMs = systemNowMs();
		sendReliable(session.peer, encodePlayerState(message));
	}

	bool savePlayerForSession(ClientSession &session)
	{
		if (!session.playerSession.authenticated)
		{
			return true;
		}
		if (!playerTable.isOpen())
		{
			return false;
		}
		if (!playerTable.savePlayer(session.player))
		{
			std::cerr << "Failed to save player "
					  << session.player.cold.username
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

	void flushDirtyChunks(size_t maxCount)
	{
		size_t savedCount = 0;
		while (savedCount < maxCount && !dirtyChunkQueue.empty())
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

				if (!worldTable.saveChunk(worldIt->second))
				{
					std::cerr << "Failed to save world chunk "
							  << worldIt->second.chunkX << ","
							  << worldIt->second.chunkZ << ": "
							  << worldTable.lastError() << std::endl;
					if (queuedDirtyChunkKeys.insert(key).second)
					{
						dirtyChunkQueue.push_back(key);
				}
				break;
			}

			dirtyChunkKeys.erase(key);
			savedCount++;
		}
	}

	void handleLoginRequest(ENetPeer *peer, const LoginRequestMessage &request)
	{
		auto sessionIt = clients.find(peer);
		if (sessionIt == clients.end())
		{
			return;
		}

		ClientSession &session = sessionIt->second;
		session.playerSession.lastSeenAtMs = systemNowMs();
		if (session.playerSession.authenticated)
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
		std::string passwordHashForNewPlayer;
		if (!password.empty())
		{
			if (!passwordHasher.hashPassword(password, passwordHashForNewPlayer))
			{
				std::cerr << "Failed to hash password for "
						  << trimmedUsername
						  << ": " << passwordHasher.lastError() << std::endl;
				response.status = LoginStatus::InvalidCredentials;
				sendReliable(peer, encodeLoginResponse(response));
				return;
			}
		}

		bool createdPlayer = false;
		PlayerData loadedPlayer;
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

		session.player = loadedPlayer;
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
			else if (!password.empty())
			{
				if (!playerTable.updatePasswordHash(
						session.player.cold.playerId,
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

		session.playerSession.playerId = session.player.cold.playerId;
		session.playerSession.authenticated = true;
		session.usernameKey = trimmedUsername;
		activeUsernames[trimmedUsername] = session.player.cold.playerId;

		response.status = LoginStatus::Accepted;
		response.playerId = session.player.cold.playerId;
		copyPlayerUsernameToBuffer(trimmedUsername, response.username);
		response.skinId = session.player.cold.skinId;
		response.positionX = session.player.hot.position.x;
		response.positionY = session.player.hot.position.y;
		response.positionZ = session.player.hot.position.z;
		response.blockActionReadyAtMs = session.player.hot.blockActionReadyAtMs;
		sendReliable(peer, encodeLoginResponse(response));
		sendReliable(peer, encodeWorldFrontier(frontier));

		std::cout << "Player authenticated: " << trimmedUsername
				  << " id=" << session.player.cold.playerId;
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
		sessionIt->second.playerSession.lastSeenAtMs = systemNowMs();
		if (!sessionIt->second.playerSession.authenticated)
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
		if (nowMs < session.player.hot.blockActionReadyAtMs)
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
		session.player.hot.blockActionReadyAtMs = nowMs + PLAYER_DEFAULT_BLOCK_ACTION_COOLDOWN_MS;

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

		session.player.hot.position = glm::vec3(
			movement.positionX,
			movement.positionY,
			movement.positionZ);
		session.player.hot.lookDirection = glm::vec3(
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
						 WorldGenerationMode generationMode,
						 std::string playerDatabasePath,
						 std::string worldDatabasePath,
						 ServerEnvironmentOptions environmentOptions)
{
	m_impl = new Impl(
		port,
		std::move(generator),
		generationMode,
		std::move(playerDatabasePath),
		std::move(worldDatabasePath),
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
