#ifndef CLIENT_CHUNK_MESHER_H
#define CLIENT_CHUNK_MESHER_H

#include <Chunk2.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

struct ClientChunkMeshJob
{
	int chunkX = 0;
	int chunkZ = 0;
	uint64_t revision = 0;
	VoxelChunkData center;
	Chunk2::MeshNeighborhood neighbors;
};

static_assert(sizeof(ClientChunkMeshJob) <= 131072,
			  "ClientChunkMeshJob must stay below 128 KiB");

struct ClientChunkMeshResult
{
	int chunkX = 0;
	int chunkZ = 0;
	uint64_t revision = 0;
	std::vector<uint32_t> packedFaces;
};

class ClientChunkMesher
{
public:
	ClientChunkMesher() = default;

	~ClientChunkMesher()
	{
		stop();
	}

	void start(size_t requestedWorkerCount = 0)
	{
		stop();

		size_t workerCount = requestedWorkerCount;
		if (workerCount == 0)
		{
			workerCount = computeWorkerCount();
		}

		{
			std::lock_guard<std::mutex> queueLock(queueMutex);
			running = true;
			activeWorkerCount = workerCount;
		}

		for (size_t index = 0; index < workerCount; index++)
		{
			workers.emplace_back(&ClientChunkMesher::workerLoop, this);
		}
	}

	void stop()
	{
		{
			std::lock_guard<std::mutex> queueLock(queueMutex);
			if (!running && workers.empty())
			{
				return;
			}
			running = false;
		}

		queueCv.notify_all();

		for (std::thread &worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
		workers.clear();

		{
			std::lock_guard<std::mutex> queueLock(queueMutex);
			pendingJobs.clear();
			activeWorkerCount = 0;
		}
		{
			std::lock_guard<std::mutex> completedLock(completedMutex);
			completedJobs.clear();
		}
	}

	void enqueue(ClientChunkMeshJob job)
	{
		{
			std::lock_guard<std::mutex> queueLock(queueMutex);
			if (!running)
			{
				return;
			}
			pendingJobs.push_back(std::move(job));
		}
		queueCv.notify_one();
	}

	void drainCompleted(std::vector<ClientChunkMeshResult> &outResults)
	{
		std::lock_guard<std::mutex> completedLock(completedMutex);
		while (!completedJobs.empty())
		{
			outResults.push_back(std::move(completedJobs.front()));
			completedJobs.pop_front();
		}
	}

	size_t workerCount() const
	{
		std::lock_guard<std::mutex> queueLock(queueMutex);
		return activeWorkerCount;
	}

	size_t pendingJobCount() const
	{
		std::lock_guard<std::mutex> queueLock(queueMutex);
		return pendingJobs.size();
	}

	size_t completedJobCount() const
	{
		std::lock_guard<std::mutex> completedLock(completedMutex);
		return completedJobs.size();
	}

private:
		static size_t computeWorkerCount()
		{
			unsigned int reported = std::thread::hardware_concurrency();
			if (reported == 0)
			{
				return 1;
			}

			// Heuristique mesurée pour le meshing client:
			// sur une machine locale client+serveur, ajouter plus de workers mesh
			// n'a pas amélioré le throughput utile. Au contraire, 1 worker est
			// souvent meilleur ou équivalent à 2/4, avec moins de pression cache.
			//
			// On reste donc très conservateur par défaut:
			// - petites et moyennes machines (jusqu'à 16 threads logiques): 1 worker
			// - machines plus grosses: 2 workers max par défaut
			//
			// Si un bench ultérieur montre qu'une machine dédiée client peut monter
			// plus haut, on garde la possibilité d'override via l'env côté main3.cpp.
			if (reported <= 16)
			{
				return 1;
			}
			return 2;
		}

	void workerLoop()
	{
		while (true)
		{
			ClientChunkMeshJob job;
			{
				std::unique_lock<std::mutex> queueLock(queueMutex);
				queueCv.wait(queueLock, [&]()
							 { return !running || !pendingJobs.empty(); });

				if (!running && pendingJobs.empty())
				{
					return;
				}

				job = std::move(pendingJobs.front());
				pendingJobs.pop_front();
			}

				ClientChunkMeshResult result;
				result.chunkX = job.chunkX;
				result.chunkZ = job.chunkZ;
				result.revision = job.revision;
				result.packedFaces = Chunk2::buildPackedFaces(job.center, job.neighbors);

			{
				std::lock_guard<std::mutex> completedLock(completedMutex);
				completedJobs.push_back(std::move(result));
			}
		}
	}

	mutable std::mutex queueMutex;
	std::condition_variable queueCv;
	std::deque<ClientChunkMeshJob> pendingJobs;
	std::vector<std::thread> workers;
	bool running = false;
	size_t activeWorkerCount = 0;

	mutable std::mutex completedMutex;
	std::deque<ClientChunkMeshResult> completedJobs;
};

#endif
