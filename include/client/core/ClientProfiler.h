#ifndef CLIENT_CORE_CLIENT_PROFILER_H
#define CLIENT_CORE_CLIENT_PROFILER_H

#include <array>
#include <chrono>
#include <cstddef>

struct ClientProfilerWindowSummary
{
	bool valid = false;
	double avgFps = 0.0;
	double avgFrameMs = 0.0;
	double maxFrameMs = 0.0;
	double avgRenderMs = 0.0;
	double avgTracked = 0.0;
	size_t maxTracked = 0;
	double avgQueued = 0.0;
	size_t maxQueued = 0;
	double avgReady = 0.0;
	size_t maxReady = 0;
	double avgVisible = 0.0;
	size_t maxVisible = 0;
	double avgStreamed = 0.0;
	size_t maxStreamed = 0;
	size_t requestsWindow = 0;
	double requestsPerSecond = 0.0;
	size_t dropsWindow = 0;
	double dropsPerSecond = 0.0;
	size_t receivesWindow = 0;
	double receivesPerSecond = 0.0;
	size_t unloadsWindow = 0;
	double unloadsPerSecond = 0.0;
	double receiveRequestRatio = 0.0;
	size_t meshedChunks = 0;
	double avgMeshedSections = 0.0;
	size_t meshWorkerCount = 0;
	int renderDistanceChunks = 0;
	bool sortVisibleChunksFrontToBack = true;
	int cameraChunkX = 0;
	int cameraChunkZ = 0;
	double windowSeconds = 0.0;
};

struct ClientProfilerState
{
	static constexpr size_t HISTORY_CAPACITY = 120;

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
	std::array<float, HISTORY_CAPACITY> frameMsHistory = {};
	std::array<float, HISTORY_CAPACITY> renderMsHistory = {};
	std::array<float, HISTORY_CAPACITY> visibleHistory = {};
	std::array<float, HISTORY_CAPACITY> trackedHistory = {};
	std::array<float, HISTORY_CAPACITY> queuedHistory = {};
	std::array<float, HISTORY_CAPACITY> readyHistory = {};
	std::array<float, HISTORY_CAPACITY> requestsHistory = {};
	std::array<float, HISTORY_CAPACITY> receivesHistory = {};
	size_t historyCount = 0;
	size_t historyCursor = 0;
	float latestFrameMs = 0.0f;
	float latestRenderMs = 0.0f;
	size_t latestVisible = 0;
	size_t latestTracked = 0;
	size_t latestQueued = 0;
	size_t latestReady = 0;
	size_t latestRequests = 0;
	size_t latestReceives = 0;
	ClientProfilerWindowSummary latestWindow;
};

void clearClientProfilerState(ClientProfilerState &state,
							  std::chrono::steady_clock::time_point now);

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
									  size_t &profileMeshedSectionCountWindow,
									  bool emitConsoleLog);

#endif
