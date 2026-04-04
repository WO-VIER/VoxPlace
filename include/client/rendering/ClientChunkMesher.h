#ifndef CLIENT_CHUNK_MESHER_H
#define CLIENT_CHUNK_MESHER_H

#include <ClientChunk.h>

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
	ClientChunk::MeshNeighborhood neighbors;
};

static_assert(sizeof(ClientChunkMeshJob) <= 131072,
			  "ClientChunkMeshJob must stay below 128 KiB");

struct ClientChunkMeshResult
{
	int chunkX = 0;
	int chunkZ = 0;
	uint64_t revision = 0;
	uint8_t nonEmptySectionCount = 0;
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

	void start(size_t requestedWorkerCount = 0, bool isLocalConnection = true)
	{
		stop();

		size_t workerCount = requestedWorkerCount;
		if (workerCount == 0)
		{
			workerCount = computeWorkerCount(isLocalConnection);
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
	static size_t computeWorkerCount(bool isLocalConnection)
		{
			unsigned int reported = std::thread::hardware_concurrency();
			if (reported == 0)
			{
				return 1;
			}

			// Heuristique mesurée pour le meshing client (TFE Benchmark):
			// 1. Sur une machine LOCALE (Client + Serveur sur le même CPU 8 coeurs):
			//    On reste conservateur pour laisser les ressources à la génération de terrain (FastNoise).
			//    => 1 worker par défaut sur les machines < 16 threads.
			//
			// 2. Sur une machine DISTANTE (Client Only, Serveur sur VPS):
			//    On peut utiliser plus de parallélisme. Le benchmark a montré un "Sweet Spot" à 4 workers.
			//    => Environ 50% des coeurs disponibles (max 4 pour garantir la fluidité du thread de rendu).
			if (isLocalConnection)
			{
				if (reported <= 16)
				{
					return 1;
				}
				return 2;
			}
			else
			{
				size_t count = static_cast<size_t>(reported) / 2;
				if (count < 1)
				{
					return 1;
				}
				
				if (count > 4)
				{
					return 4;
				}
				return count;
			}
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
					result.nonEmptySectionCount = static_cast<uint8_t>(job.center.nonEmptySectionCount());
					result.packedFaces = ClientChunk::buildPackedFaces(job.center, job.neighbors);

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
