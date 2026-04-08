#include <client/ui/ProfilerWindows.h>

#include <VoxelChunkData.h>
#include <client/gameplay/CameraBoundsSystem.h>

#include <cfloat>

#include <imgui.h>

namespace
{
	struct HistorySeriesView
	{
		const float *values = nullptr;
		size_t count = 0;
		size_t cursor = 0;
		size_t capacity = 0;
	};

	float historySeriesGetter(void *data, int index)
	{
		HistorySeriesView *view = static_cast<HistorySeriesView *>(data);
		if (view == nullptr || view->values == nullptr || view->count == 0)
		{
			return 0.0f;
		}

		size_t actualIndex = static_cast<size_t>(index);
		if (view->count >= view->capacity)
		{
			actualIndex += view->cursor;
			if (actualIndex >= view->capacity)
			{
				actualIndex -= view->capacity;
			}
		}
		return view->values[actualIndex];
	}

	template <size_t N>
	void renderHistoryPlot(const char *label,
						   const std::array<float, N> &values,
						   size_t count,
						   size_t cursor)
	{
		if (count == 0)
		{
			ImGui::Text("%s: collecting...", label);
			return;
		}

		HistorySeriesView view;
		view.values = values.data();
		view.count = count;
		view.cursor = cursor;
		view.capacity = N;
		ImGui::PlotLines(
			label,
			historySeriesGetter,
			&view,
			static_cast<int>(count),
			0,
			nullptr,
			FLT_MAX,
			FLT_MAX,
			ImVec2(0.0f, 80.0f));
	}

	void renderLegendItem(const char *label, ImU32 color)
	{
		ImVec4 colorValue = ImGui::ColorConvertU32ToFloat4(color);
		ImGui::ColorButton(label, colorValue, ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f));
		ImGui::SameLine();
		ImGui::Text("%s", label);
	}

	const char *yesNoText(bool value)
	{
		if (value)
		{
			return "yes";
		}
		return "no";
	}

	const char *onOffText(bool value)
	{
		if (value)
		{
			return "on";
		}
		return "off";
	}

	ImU32 chunkMapCellColor(bool loaded,
							 bool inFrustum,
							 bool pendingMesh,
							 bool streamed)
	{
		if (pendingMesh)
		{
			return IM_COL32(194, 168, 61, 255);
		}
		if (loaded)
		{
			if (inFrustum)
			{
				return IM_COL32(63, 181, 92, 255);
			}
			return IM_COL32(143, 58, 58, 255);
		}
		if (streamed)
		{
			return IM_COL32(65, 113, 191, 255);
		}
		return IM_COL32(55, 55, 55, 255);
	}

	void renderChunkMapTooltip(const ProfilerWindowsData &data,
							   int chunkX,
							   int chunkZ,
							   bool loaded,
							   bool inFrustum,
							   bool pendingMesh,
							   bool streamed)
	{
		if (!ImGui::IsItemHovered())
		{
			return;
		}

		ImGui::BeginTooltip();
		ImGui::Text("Chunk (%d, %d)", chunkX, chunkZ);
		ImGui::Text("Loaded: %s", yesNoText(loaded));
		ImGui::Text("In frustum: %s", yesNoText(inFrustum));
		ImGui::Text("Pending mesh: %s", yesNoText(pendingMesh));
		ImGui::Text("Stream tracked: %s", yesNoText(streamed));
		if (data.worldState != nullptr && data.worldState->hasWorldFrontier)
		{
			bool inPlayable = data.worldState->frontier.playableBounds.containsChunk(chunkX, chunkZ);
			bool inGenerated = data.worldState->frontier.generatedBounds.containsChunk(chunkX, chunkZ);
			ImGui::Text("Playable bounds: %s", yesNoText(inPlayable));
			ImGui::Text("Generated bounds: %s", yesNoText(inGenerated));
		}
		ImGui::EndTooltip();
	}

	void renderChunkSystemMap(const ProfilerWindowsData &data)
	{
		if (data.gameState == nullptr ||
			data.worldState == nullptr ||
			data.camera == nullptr ||
			data.frameContext == nullptr)
		{
			ImGui::Text("Chunk map unavailable.");
			return;
		}

		static float cellSize = 10.0f;
		ImGui::SliderFloat("Cell Size", &cellSize, 6.0f, 20.0f);
		renderLegendItem("Not loaded", IM_COL32(55, 55, 55, 255));
		ImGui::SameLine();
		renderLegendItem("Tracked", IM_COL32(65, 113, 191, 255));
		ImGui::SameLine();
		renderLegendItem("Pending mesh", IM_COL32(194, 168, 61, 255));
		renderLegendItem("Visible", IM_COL32(63, 181, 92, 255));
		ImGui::SameLine();
		renderLegendItem("Culled", IM_COL32(143, 58, 58, 255));
		ImGui::Text("White outline = camera chunk");

		CameraChunkCoord cameraChunk = cameraChunkCoord(*data.camera);
		int radius = data.gameState->render.renderDistanceChunks + 2;
		if (radius < 4)
		{
			radius = 4;
		}

		int minChunkX = cameraChunk.x - radius;
		int maxChunkX = cameraChunk.x + radius;
		int minChunkZ = cameraChunk.z - radius;
		int maxChunkZ = cameraChunk.z + radius;
		int width = maxChunkX - minChunkX + 1;
		int height = maxChunkZ - minChunkZ + 1;

		ImVec2 canvasSize(
			cellSize * static_cast<float>(width),
			cellSize * static_cast<float>(height));

		ImGui::BeginChild("ClientChunkMapRegion",
						  ImVec2(0.0f, 360.0f),
						  true,
						  ImGuiWindowFlags_HorizontalScrollbar);
		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("ClientChunkMapCanvas", canvasSize);
		ImDrawList *drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(origin,
								ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y),
								IM_COL32(20, 20, 20, 255));

		for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; chunkZ++)
		{
			for (int chunkX = minChunkX; chunkX <= maxChunkX; chunkX++)
			{
				int64_t key = chunkKey(chunkX, chunkZ);
				bool loaded = data.worldState->chunkMap.find(key) != data.worldState->chunkMap.end();
				bool pendingMesh = data.worldState->pendingMeshRevisions.find(key) != data.worldState->pendingMeshRevisions.end();
				bool streamed = data.worldState->streamedChunkKeys.find(key) != data.worldState->streamedChunkKeys.end();
				bool inFrustum = data.frameContext->frustum.isChunkVisible(chunkX, chunkZ);

				float xIndex = static_cast<float>(chunkX - minChunkX);
				float zIndex = static_cast<float>(chunkZ - minChunkZ);
				ImVec2 cellMin(
					origin.x + xIndex * cellSize,
					origin.y + zIndex * cellSize);
				ImVec2 cellMax(
					cellMin.x + cellSize - 1.0f,
					cellMin.y + cellSize - 1.0f);
				ImU32 cellColor = chunkMapCellColor(
					loaded,
					inFrustum,
					pendingMesh,
					streamed);

				drawList->AddRectFilled(cellMin, cellMax, cellColor);
				if (chunkX == cameraChunk.x && chunkZ == cameraChunk.z)
				{
					drawList->AddRect(cellMin, cellMax, IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
				}
			}
		}

		if (ImGui::IsItemHovered())
		{
			ImVec2 mouse = ImGui::GetIO().MousePos;
			float localX = mouse.x - origin.x;
			float localY = mouse.y - origin.y;
			if (localX >= 0.0f && localY >= 0.0f)
			{
				int hoverX = static_cast<int>(localX / cellSize);
				int hoverZ = static_cast<int>(localY / cellSize);
				if (hoverX >= 0 && hoverX < width && hoverZ >= 0 && hoverZ < height)
				{
					int chunkX = minChunkX + hoverX;
					int chunkZ = minChunkZ + hoverZ;
					int64_t key = chunkKey(chunkX, chunkZ);
					bool loaded = data.worldState->chunkMap.find(key) != data.worldState->chunkMap.end();
					bool pendingMesh = data.worldState->pendingMeshRevisions.find(key) != data.worldState->pendingMeshRevisions.end();
					bool streamed = data.worldState->streamedChunkKeys.find(key) != data.worldState->streamedChunkKeys.end();
					bool inFrustum = data.frameContext->frustum.isChunkVisible(chunkX, chunkZ);
					renderChunkMapTooltip(
						data,
						chunkX,
						chunkZ,
						loaded,
						inFrustum,
						pendingMesh,
						streamed);
				}
			}
		}

		ImGui::EndChild();
	}

	void renderClientProfilerWindow(const ProfilerWindowsData &data)
	{
		if (!ImGui::Begin("Client Profiler"))
		{
			ImGui::End();
			return;
		}

		if (data.clientProfiler == nullptr)
		{
			ImGui::Text("Client profiler unavailable.");
			ImGui::End();
			return;
		}

		const ClientProfilerState &profiler = *data.clientProfiler;
		const ClientProfilerWindowSummary &summary = profiler.latestWindow;

		ImGui::Text("Latest Frame: %.2f ms", profiler.latestFrameMs);
		ImGui::Text("Latest Render CPU: %.2f ms", profiler.latestRenderMs);
		ImGui::Text("Latest Visible Chunks: %zu", profiler.latestVisible);
		ImGui::Text("Latest Mesh Jobs: %zu tracked / %zu queued / %zu ready",
					profiler.latestTracked,
					profiler.latestQueued,
					profiler.latestReady);
		ImGui::Text("Latest Network: %zu requests / %zu receives",
					profiler.latestRequests,
					profiler.latestReceives);

		renderHistoryPlot(
			"Frame ms",
			profiler.frameMsHistory,
			profiler.historyCount,
			profiler.historyCursor);
		renderHistoryPlot(
			"Render CPU ms",
			profiler.renderMsHistory,
			profiler.historyCount,
			profiler.historyCursor);
		renderHistoryPlot(
			"Visible chunks",
			profiler.visibleHistory,
			profiler.historyCount,
			profiler.historyCursor);
		renderHistoryPlot(
			"Tracked jobs",
			profiler.trackedHistory,
			profiler.historyCount,
			profiler.historyCursor);

		if (summary.valid)
		{
			ImGui::Separator();
			ImGui::Text("Window: %.1f s", summary.windowSeconds);
			ImGui::Text("FPS avg: %.1f", summary.avgFps);
			ImGui::Text("Frame avg/max: %.3f / %.3f ms",
						summary.avgFrameMs,
						summary.maxFrameMs);
			ImGui::Text("Render CPU avg: %.3f ms", summary.avgRenderMs);
			ImGui::Text("Workers: %zu", summary.meshWorkerCount);
			ImGui::Text("Tracked avg/max: %.2f / %zu",
						summary.avgTracked,
						summary.maxTracked);
			ImGui::Text("Queued avg/max: %.2f / %zu",
						summary.avgQueued,
						summary.maxQueued);
			ImGui::Text("Ready avg/max: %.2f / %zu",
						summary.avgReady,
						summary.maxReady);
			ImGui::Text("Visible avg/max: %.2f / %zu",
						summary.avgVisible,
						summary.maxVisible);
			ImGui::Text("Streamed avg/max: %.2f / %zu",
						summary.avgStreamed,
						summary.maxStreamed);
			ImGui::Text("Requests: %zu (%.2f/s)",
						summary.requestsWindow,
						summary.requestsPerSecond);
			ImGui::Text("Receives: %zu (%.2f/s)",
						summary.receivesWindow,
						summary.receivesPerSecond);
			ImGui::Text("Drops: %zu (%.2f/s)",
						summary.dropsWindow,
						summary.dropsPerSecond);
			ImGui::Text("Unloads: %zu (%.2f/s)",
						summary.unloadsWindow,
						summary.unloadsPerSecond);
			ImGui::Text("Receive / Request Ratio: %.3f", summary.receiveRequestRatio);
			ImGui::Text("Meshed chunks: %zu", summary.meshedChunks);
			ImGui::Text("Avg meshed sections: %.2f", summary.avgMeshedSections);
			ImGui::Text("Camera chunk: (%d, %d)",
						summary.cameraChunkX,
						summary.cameraChunkZ);
			ImGui::Text("Render distance: %d", summary.renderDistanceChunks);
			ImGui::Text("Front-to-back sorting: %s",
						onOffText(summary.sortVisibleChunksFrontToBack));
		}
		else
		{
			ImGui::Separator();
			ImGui::Text("Window summary: collecting...");
		}

		ImGui::Separator();
		ImGui::Text("Chunk System");
		renderChunkSystemMap(data);
		ImGui::End();
	}

	void renderServerWindow(const ProfilerWindowsData &data)
	{
		if (!ImGui::Begin("Server window"))
		{
			ImGui::End();
			return;
		}

		if (data.worldState == nullptr || !data.worldState->hasServerProfile)
		{
			ImGui::Text("Waiting for server telemetry...");
			ImGui::End();
			return;
		}

		const ServerProfileMessage &profile = data.worldState->serverProfile;
		ImGui::Text("Mode: %s", worldGenerationModeName(profile.mode));
		ImGui::Text("Window: %.1f s", profile.windowSeconds);
		ImGui::Text("Ticks per second: %.2f", profile.ticksPerSecond);
		ImGui::Text("Workers: %u", static_cast<unsigned int>(profile.workerCount));
		ImGui::Text("Clients: %u", static_cast<unsigned int>(profile.clientCount));
		ImGui::Text("World chunks: %u", profile.worldChunkCount);

		ImGui::Separator();
		ImGui::Text("Ready/Load");
		ImGui::Text("Ready window: %u", profile.readyWindow);
		ImGui::Text("Loaded window: %u", profile.loadedWindow);
		ImGui::Text("Generated fresh window: %u", profile.generatedFreshWindow);
		ImGui::Text("Load errors window: %u", profile.loadErrorsWindow);
		ImGui::Text("Integrated window: %u", profile.integratedWindow);
		ImGui::Text("Integrated loaded/generated: %u / %u",
					profile.integratedLoadedWindow,
					profile.integratedGeneratedWindow);
		ImGui::Text("Unloaded window: %u", profile.unloadedWindow);

		ImGui::Separator();
		ImGui::Text("Queues");
		ImGui::Text("Queued for send window: %u", profile.queuedForSendWindow);
		ImGui::Text("Send queue now: %u", profile.sendQueueNow);
		ImGui::Text("Dirty marked window: %u", profile.dirtyMarkedWindow);
		ImGui::Text("Dirty queue now: %u", profile.dirtyQueueNow);
		ImGui::Text("Save queue jobs now: %u", profile.saveQueueJobsNow);

		ImGui::Separator();
		ImGui::Text("Workers");
		ImGui::Text("Tasks now/avg/max: %u / %.2f / %u",
					profile.tasksNow,
					profile.tasksAvg,
					profile.tasksMax);
		ImGui::Text("Ready now/avg/max: %u / %.2f / %u",
					profile.readyNow,
					profile.readyAvg,
					profile.readyMax);

		ImGui::Separator();
		ImGui::Text("Snapshots");
		ImGui::Text("Snapshot count: %u", profile.snapshotCount);
		ImGui::Text("Snapshot avg bytes/raw: %.1f / %.1f",
					profile.snapshotAvgBytes,
					profile.snapshotAvgRawBytes);
		ImGui::Text("Snapshot avg sections: %.2f", profile.snapshotAvgSections);
		ImGui::Text("Snapshot ratio: %.3f", profile.snapshotRatio);
		ImGui::Text("Load DB total/avg/max: %.2f / %.3f / %.3f ms",
					profile.sqliteLoadChunkMsTotal,
					profile.sqliteLoadChunkMsAvg,
					profile.sqliteLoadChunkMsMax);
		ImGui::Text("Gen total/avg/max: %.2f / %.3f / %.3f ms",
					profile.terrainGenChunkMsTotal,
					profile.terrainGenChunkMsAvg,
					profile.terrainGenChunkMsMax);
		ImGui::Text("Saved chunks window: %u", profile.savedChunksWindow);
		ImGui::Text("Save batches window: %u", profile.saveBatchesWindow);
		ImGui::Text("Avg chunks per save batch: %.2f", profile.saveAvgChunks);
		ImGui::End();
	}
}

void updateProfilerWindowsToggle(GLFWwindow *window, bool inGame, bool &visible)
{
	static bool previousF12Down = false;
	if (!inGame)
	{
		previousF12Down = false;
		return;
	}

	bool currentF12Down = glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS;
	if (currentF12Down && !previousF12Down)
	{
		visible = !visible;
		if (visible)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
	previousF12Down = currentF12Down;
}

void renderProfilerWindows(bool visible, const ProfilerWindowsData &data)
{
	if (!visible)
	{
		return;
	}

	renderClientProfilerWindow(data);
	renderServerWindow(data);
}
