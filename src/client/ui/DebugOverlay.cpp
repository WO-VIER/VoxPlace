#include <client/ui/DebugOverlay.h>

void updateDebugOverlayToggle(GLFWwindow *window, bool inGame, bool &visible)
{
	static bool previousF3Down = false;
	if (!inGame)
	{
		previousF3Down = false;
		return;
	}

	bool currentF3Down = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
	if (currentF3Down && !previousF3Down)
	{
		visible = !visible;
		if (visible)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}
	previousF3Down = currentF3Down;
}

void renderDebugOverlay(bool visible, const DebugOverlayData &data)
{
	if (!visible)
	{
		return;
	}

	ImGui::Begin("VoxPlace");
	if (data.deltaTime > 0.0f)
	{
		ImGui::Text("FPS: %.0f (%.1f ms)", 1.0f / data.deltaTime, data.deltaTime * 1000.0f);
	}
	ImGui::Text("Server: %s:%d", data.serverHost, data.serverPort);
	ImGui::Text("Username: %s", data.username);
	ImGui::Text("Network: %s (%u ms ping)", data.connected ? "Connected" : "Disconnected", data.roundTripTimeMs);
	if (data.hasWorldFrontier && data.frontier != nullptr)
	{
		ImGui::Text("World mode: %s", worldGenerationModeName(data.frontier->mode));
	}
	ImGui::Separator();
	ImGui::Text("Chunks: %d / %zu visible", data.visibleChunks, data.loadedChunkCount);
	ImGui::Text("Chunk render CPU: %.3f ms", data.chunkRenderCpuMs);
	if (data.usesIndirectRendering)
	{
		ImGui::Text("Draw mode: Indirect");
		ImGui::Text("CPU draw calls: %d", data.cpuDrawCalls);
		ImGui::Text("Indirect commands: %zu", data.indirectDrawCount);
		ImGui::Text("Indirect faces: %zu", data.indirectFaceCount);
		ImGui::Text("Indirect draw data: %s (%llu rebuilds)",
					data.indirectReused ? "reused" : "rebuilt",
					static_cast<unsigned long long>(data.indirectBuildCount));
		ImGui::Text("Arena usage: %.2f / %.2f MB",
					data.indirectArenaReservedMb,
					data.indirectArenaCapacityMb);
		ImGui::Text("Arena used faces: %zu", data.indirectArenaUsedFaces);
		ImGui::Text("Largest free span: %zu faces", data.indirectLargestFreeSpan);
		if (ImGui::Button("Compact Arena") && data.compactArenaRequested != nullptr)
		{
			*data.compactArenaRequested = true;
		}
	}
	else
	{
		ImGui::Text("Draw mode: Direct");
		ImGui::Text("CPU draw calls: %d", data.cpuDrawCalls);
	}

	ImGui::Text("Mesh workers: %zu", data.meshWorkerCount);
	ImGui::Text("Mesh jobs: %zu tracked / %zu queued / %zu ready",
				data.trackedJobs,
				data.queuedJobs,
				data.readyJobs);

	if (data.renderDistanceChunks != nullptr)
	{
		ImGui::SliderInt("Render Dist (chunks)", data.renderDistanceChunks, 2, 32);
	}
	ImGui::Text("Far Plane: %.1f", data.farPlane);

	if (data.limitToPlayableWorld != nullptr)
	{
		if (data.hasWorldFrontier && data.frontier != nullptr)
		{
			if (data.usesClassicStreaming)
			{
				ImGui::Text("Classic streaming enabled");
				ImGui::Text("Playable bounds clamp disabled in this mode");
				if (data.classicStreamingPaddingChunks != nullptr)
				{
					ImGui::SliderInt("Classic Stream Pad", data.classicStreamingPaddingChunks, 0, 8);
				}
			}
			else
			{
				ImGui::Checkbox("Limit to playable world", data.limitToPlayableWorld);
				ImGui::Text("Playable chunks: %d x %d",
							data.frontier->playableBounds.widthChunks(),
							data.frontier->playableBounds.depthChunks());
				ImGui::Text("Generated chunks: %d x %d",
							data.frontier->generatedBounds.widthChunks(),
							data.frontier->generatedBounds.depthChunks());
				ImGui::Text("Padding chunks: %d", data.frontier->paddingChunks);
				ImGui::Text("Expansion progress: %d / %d",
							data.frontier->activePlayableChunkCount,
							data.frontier->requiredActiveChunkCount);
			}
		}
		else
		{
			ImGui::Checkbox("Limit to playable world", data.limitToPlayableWorld);
		}
	}

	ImGui::Separator();
	ImGui::Text("Camera: (%.1f, %.1f, %.1f)", data.cameraX, data.cameraY, data.cameraZ);
	ImGui::Text("Heading: %s", data.headingText);
	if (data.cameraSpeed != nullptr)
	{
		ImGui::SliderFloat("Speed", data.cameraSpeed, 1.0f, 100.0f);
	}

	ImGui::Separator();
	ImGui::Text("Fog");
	if (data.minecraftFogByRenderDistance != nullptr)
	{
		ImGui::Checkbox("Fog (Render Dist)", data.minecraftFogByRenderDistance);
		if (*data.minecraftFogByRenderDistance)
		{
			if (data.minecraftFogStartPercent != nullptr)
			{
				ImGui::SliderFloat("Fog Start %%", data.minecraftFogStartPercent, 0.55f, 0.98f);
			}
			ImGui::Text("Fog End = Far Plane");
		}
		else
		{
			float fogSliderMax = data.farPlane;
			if (fogSliderMax < 500.0f)
			{
				fogSliderMax = 500.0f;
			}
			if (data.fogStart != nullptr)
			{
				ImGui::SliderFloat("Fog Start", data.fogStart, 0.0f, fogSliderMax);
			}
			if (data.fogEnd != nullptr)
			{
				ImGui::SliderFloat("Fog End", data.fogEnd, 0.0f, fogSliderMax);
			}
		}
	}
	ImGui::Text("Fog active: %.1f -> %.1f", data.fogStartFrame, data.fogEndFrame);

	ImGui::Separator();
	if (data.useAO != nullptr)
	{
		ImGui::Checkbox("Ambient Occlusion", data.useAO);
	}
	if (data.debugSunblockOnly != nullptr)
	{
		ImGui::Checkbox("Sunblock debug", data.debugSunblockOnly);
	}
	ImGui::Text("Terrain Architecture: %s", data.terrainArchitectureName);
	if (data.terrainArchitectureIndex != nullptr)
	{
		ImGui::SliderInt(
			"Terrain Arch",
			data.terrainArchitectureIndex,
			data.terrainArchitectureMin,
			data.terrainArchitectureMax);
	}
	if (data.crosshairVisible != nullptr)
	{
		ImGui::Checkbox("Crosshair", data.crosshairVisible);
	}
	ImGui::Text("Left click: break block");
	ImGui::Text("Right click: place block");
	ImGui::End();
}
