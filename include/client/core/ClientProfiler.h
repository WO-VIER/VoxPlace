#ifndef CLIENT_CORE_CLIENT_PROFILER_H
#define CLIENT_CORE_CLIENT_PROFILER_H

#include <chrono>
#include <cstddef>

struct ClientProfilerState
{
	std::chrono::steady_clock::time_point windowStart;
	double accumFrameMs = 0.0;
	double maxFrameMs = 0.0;
	double accumRenderMs = 0.0;
	size_t sampleCount = 0;
	size_t accumTracked = 0;
	size_t maxTracked = 0;
	size_t accumQueued = 0;
	size_t maxQueued = 0;
	size_t accumReady = 0;
	size_t maxReady = 0;
	size_t accumVisible = 0;
	size_t maxVisible = 0;
	size_t accumStreamed = 0;
	size_t maxStreamed = 0;
	size_t accumRequests = 0;
	size_t maxRequests = 0;
	size_t accumDrops = 0;
	size_t maxDrops = 0;
	size_t accumReceives = 0;
	size_t maxReceives = 0;
	size_t accumUnloads = 0;
	size_t maxUnloads = 0;
};

void resetClientProfilerWindow(ClientProfilerState &state,
							   std::chrono::steady_clock::time_point now);

void accumulateClientProfilerSample(ClientProfilerState &state,
									double frameMs,
									double renderMs,
									size_t trackedJobs,
									size_t queuedJobs,
									size_t readyJobs,
									size_t visibleChunks,
									size_t streamedChunks,
									size_t requests,
									size_t drops,
									size_t receives);

bool flushClientProfilerWindowIfReady(ClientProfilerState &state,
									  std::chrono::steady_clock::time_point now,
									  size_t meshedChunks,
									  size_t meshedSections,
									  size_t meshWorkerCount,
									  int renderDistanceChunks,
									  bool sortVisibleChunksFrontToBack,
									  int cameraChunkX,
									  int cameraChunkZ,
									  size_t &profileChunkRequestsWindow,
									  size_t &profileChunkDropsWindow,
									  size_t &profileChunkReceivesWindow,
									  size_t &profileChunkUnloadsWindow,
									  size_t &profileMeshedChunkCountWindow,
									  size_t &profileMeshedSectionCountWindow);

#endif
