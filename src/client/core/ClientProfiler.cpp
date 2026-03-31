#include <client/core/ClientProfiler.h>

#include <iostream>

void resetClientProfilerWindow(ClientProfilerState &state,
							   std::chrono::steady_clock::time_point now)
{
	state.windowStart = now;
	state.accumFrameMs = 0.0;
	state.maxFrameMs = 0.0;
	state.accumRenderMs = 0.0;
	state.sampleCount = 0;
	state.accumTracked = 0;
	state.maxTracked = 0;
	state.accumQueued = 0;
	state.maxQueued = 0;
	state.accumReady = 0;
	state.maxReady = 0;
	state.accumVisible = 0;
	state.maxVisible = 0;
	state.accumStreamed = 0;
	state.maxStreamed = 0;
	state.accumRequests = 0;
	state.maxRequests = 0;
	state.accumDrops = 0;
	state.maxDrops = 0;
	state.accumReceives = 0;
	state.maxReceives = 0;
	state.accumUnloads = 0;
	state.maxUnloads = 0;
}

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
									size_t receives)
{
	state.accumFrameMs += frameMs;
	state.accumRenderMs += renderMs;
	state.sampleCount++;
	state.accumTracked += trackedJobs;
	state.accumQueued += queuedJobs;
	state.accumReady += readyJobs;
	state.accumVisible += visibleChunks;
	state.accumStreamed += streamedChunks;
	state.accumRequests += requests;
	state.accumDrops += drops;
	state.accumReceives += receives;

	if (frameMs > state.maxFrameMs)
	{
		state.maxFrameMs = frameMs;
	}
	if (trackedJobs > state.maxTracked)
	{
		state.maxTracked = trackedJobs;
	}
	if (queuedJobs > state.maxQueued)
	{
		state.maxQueued = queuedJobs;
	}
	if (readyJobs > state.maxReady)
	{
		state.maxReady = readyJobs;
	}
	if (visibleChunks > state.maxVisible)
	{
		state.maxVisible = visibleChunks;
	}
	if (streamedChunks > state.maxStreamed)
	{
		state.maxStreamed = streamedChunks;
	}
	if (requests > state.maxRequests)
	{
		state.maxRequests = requests;
	}
	if (drops > state.maxDrops)
	{
		state.maxDrops = drops;
	}
	if (receives > state.maxReceives)
	{
		state.maxReceives = receives;
	}
}

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
									  size_t &profileMeshedSectionCountWindow)
{
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.windowStart);
	if (elapsed.count() < 2000 || state.sampleCount == 0)
	{
		return false;
	}

	double avgFrameMs = state.accumFrameMs / static_cast<double>(state.sampleCount);
	double avgFps = 0.0;
	if (avgFrameMs > 0.0)
	{
		avgFps = 1000.0 / avgFrameMs;
	}
	double avgRenderMs = state.accumRenderMs / static_cast<double>(state.sampleCount);
	double avgTracked = static_cast<double>(state.accumTracked) / static_cast<double>(state.sampleCount);
	double avgQueued = static_cast<double>(state.accumQueued) / static_cast<double>(state.sampleCount);
	double avgReady = static_cast<double>(state.accumReady) / static_cast<double>(state.sampleCount);
	double avgVisible = static_cast<double>(state.accumVisible) / static_cast<double>(state.sampleCount);
	double avgStreamed = static_cast<double>(state.accumStreamed) / static_cast<double>(state.sampleCount);
	double elapsedSeconds = static_cast<double>(elapsed.count()) / 1000.0;
	double requestsPerSecond = 0.0;
	double dropsPerSecond = 0.0;
	double receivesPerSecond = 0.0;
	if (elapsedSeconds > 0.0)
	{
		requestsPerSecond =
			static_cast<double>(profileChunkRequestsWindow) / elapsedSeconds;
		dropsPerSecond =
			static_cast<double>(profileChunkDropsWindow) / elapsedSeconds;
		receivesPerSecond =
			static_cast<double>(profileChunkReceivesWindow) / elapsedSeconds;
	}
	double receiveRequestRatio = 0.0;
	if (profileChunkRequestsWindow > 0)
	{
		receiveRequestRatio =
			static_cast<double>(profileChunkReceivesWindow) /
			static_cast<double>(profileChunkRequestsWindow);
	}
	double unloadsPerSecond = 0.0;
	if (elapsedSeconds > 0.0)
	{
		unloadsPerSecond =
			static_cast<double>(profileChunkUnloadsWindow) / elapsedSeconds;
	}
	double avgMeshedSections = 0.0;
	if (meshedChunks > 0)
	{
		avgMeshedSections = static_cast<double>(meshedSections) / static_cast<double>(meshedChunks);
	}
	int sortVisibleChunksProfileValue = sortVisibleChunksFrontToBack ? 1 : 0;

	std::cout << "[client-profile] workers=" << meshWorkerCount
			  << " renderDist=" << renderDistanceChunks
			  << " sort_visible_chunks=" << sortVisibleChunksProfileValue
			  << " camera_chunk=(" << cameraChunkX << "," << cameraChunkZ << ")"
			  << " fps_avg=" << avgFps
			  << " frame_ms_avg=" << avgFrameMs
			  << " frame_ms_max=" << state.maxFrameMs
			  << " render_cpu_ms_avg=" << avgRenderMs
			  << " tracked_avg=" << avgTracked
			  << " tracked_max=" << state.maxTracked
			  << " queued_avg=" << avgQueued
			  << " queued_max=" << state.maxQueued
			  << " ready_avg=" << avgReady
			  << " ready_max=" << state.maxReady
			  << " visible_avg=" << avgVisible
			  << " visible_max=" << state.maxVisible
			  << " streamed_avg=" << avgStreamed
			  << " streamed_max=" << state.maxStreamed
			  << " requests_window=" << profileChunkRequestsWindow
			  << " requests_per_sec=" << requestsPerSecond
			  << " requests_max=" << state.maxRequests
			  << " drops_window=" << profileChunkDropsWindow
			  << " drops_per_sec=" << dropsPerSecond
			  << " drops_max=" << state.maxDrops
			  << " receives_window=" << profileChunkReceivesWindow
			  << " receives_per_sec=" << receivesPerSecond
			  << " receives_max=" << state.maxReceives
			  << " unloads_window=" << profileChunkUnloadsWindow
			  << " unloads_per_sec=" << unloadsPerSecond
			  << " receive_request_ratio=" << receiveRequestRatio
			  << " net_stream_delta="
			  << static_cast<long long>(profileChunkReceivesWindow) -
					 static_cast<long long>(profileChunkDropsWindow)
			  << " meshed_chunks=" << meshedChunks
			  << " meshed_sections_avg=" << avgMeshedSections
			  << std::endl;

	resetClientProfilerWindow(state, now);
	profileChunkRequestsWindow = 0;
	profileChunkDropsWindow = 0;
	profileChunkReceivesWindow = 0;
	profileChunkUnloadsWindow = 0;
	profileMeshedChunkCountWindow = 0;
	profileMeshedSectionCountWindow = 0;
	return true;
}
