#include <client/gameplay/CameraBoundsSystem.h>

#include <cmath>

namespace
{
	float chunkBoundsMinX(const ChunkBounds &bounds)
	{
		return static_cast<float>(bounds.minChunkX * CHUNK_SIZE_X);
	}

	float chunkBoundsMaxX(const ChunkBounds &bounds)
	{
		return static_cast<float>(bounds.maxChunkXExclusive * CHUNK_SIZE_X);
	}

	float chunkBoundsMinZ(const ChunkBounds &bounds)
	{
		return static_cast<float>(bounds.minChunkZ * CHUNK_SIZE_Z);
	}

	float chunkBoundsMaxZ(const ChunkBounds &bounds)
	{
		return static_cast<float>(bounds.maxChunkZExclusive * CHUNK_SIZE_Z);
	}
}

const char *cameraHeadingCardinal(const glm::vec3 &front)
{
	float ax = std::abs(front.x);
	float az = std::abs(front.z);
	if (ax >= az)
	{
		if (front.x < 0.0f)
		{
			return "West (-X)";
		}
		return "East (+X)";
	}
	if (front.z < 0.0f)
	{
		return "South (-Z)";
	}
	return "North (+Z)";
}

CameraChunkCoord cameraChunkCoord(const Camera &camera)
{
	CameraChunkCoord result;
	result.x = floorDiv(static_cast<int>(std::floor(camera.Position.x)), CHUNK_SIZE_X);
	result.z = floorDiv(static_cast<int>(std::floor(camera.Position.z)), CHUNK_SIZE_Z);
	return result;
}

void clampCameraToPlayableWorld(const WorldFrontier &frontier,
								bool hasWorldFrontier,
								bool limitToPlayableWorld,
								Camera &camera)
{
	if (!limitToPlayableWorld || !hasWorldFrontier)
	{
		return;
	}
	if (frontier.mode == WorldGenerationMode::ClassicStreaming)
	{
		return;
	}

	const ChunkBounds &bounds = frontier.playableBounds;
	const float margin = 0.25f;
	float minX = chunkBoundsMinX(bounds) + margin;
	float maxX = chunkBoundsMaxX(bounds) - margin;
	float minZ = chunkBoundsMinZ(bounds) + margin;
	float maxZ = chunkBoundsMaxZ(bounds) - margin;

	if (minX > maxX || minZ > maxZ)
	{
		return;
	}

	if (camera.Position.x < minX)
	{
		camera.Position.x = minX;
	}
	else if (camera.Position.x > maxX)
	{
		camera.Position.x = maxX;
	}

	if (camera.Position.z < minZ)
	{
		camera.Position.z = minZ;
	}
	else if (camera.Position.z > maxZ)
	{
		camera.Position.z = maxZ;
	}
}
