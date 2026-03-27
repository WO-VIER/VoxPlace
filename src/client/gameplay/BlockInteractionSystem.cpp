#include <client/gameplay/BlockInteractionSystem.h>

#include <VoxelChunkData.h>

#include <cmath>
#include <limits>

namespace
{
	constexpr float PLACE_REACH = 8.0f;

	bool usesClassicStreaming(const BlockInteractionState &state)
	{
		if (!state.hasWorldFrontier || state.frontier == nullptr)
		{
			return false;
		}
		return state.frontier->mode == WorldGenerationMode::ClassicStreaming;
	}
}

bool raycastPlaceTarget(const glm::vec3 &origin,
						const glm::vec3 &direction,
						float maxDist,
						const std::function<uint32_t(int, int, int)> &getBlockWorld,
						glm::ivec3 &hitBlock,
						glm::ivec3 &placeBlock)
{
	const float inf = std::numeric_limits<float>::infinity();
	if (glm::length(direction) < 1e-6f)
	{
		return false;
	}

	glm::vec3 dir = glm::normalize(direction);
	int x = static_cast<int>(std::floor(origin.x));
	int y = static_cast<int>(std::floor(origin.y));
	int z = static_cast<int>(std::floor(origin.z));
	glm::ivec3 prevCell(x, y, z);

	int stepX = 0;
	int stepY = 0;
	int stepZ = 0;
	if (dir.x > 0.0f)
	{
		stepX = 1;
	}
	else if (dir.x < 0.0f)
	{
		stepX = -1;
	}
	if (dir.y > 0.0f)
	{
		stepY = 1;
	}
	else if (dir.y < 0.0f)
	{
		stepY = -1;
	}
	if (dir.z > 0.0f)
	{
		stepZ = 1;
	}
	else if (dir.z < 0.0f)
	{
		stepZ = -1;
	}

	float tDeltaX = inf;
	float tDeltaY = inf;
	float tDeltaZ = inf;
	if (stepX != 0)
	{
		tDeltaX = std::abs(1.0f / dir.x);
	}
	if (stepY != 0)
	{
		tDeltaY = std::abs(1.0f / dir.y);
	}
	if (stepZ != 0)
	{
		tDeltaZ = std::abs(1.0f / dir.z);
	}

	float nextX = 0.0f;
	float nextY = 0.0f;
	float nextZ = 0.0f;
	if (stepX > 0)
	{
		nextX = std::floor(origin.x) + 1.0f - origin.x;
	}
	else
	{
		nextX = origin.x - std::floor(origin.x);
	}
	if (stepY > 0)
	{
		nextY = std::floor(origin.y) + 1.0f - origin.y;
	}
	else
	{
		nextY = origin.y - std::floor(origin.y);
	}
	if (stepZ > 0)
	{
		nextZ = std::floor(origin.z) + 1.0f - origin.z;
	}
	else
	{
		nextZ = origin.z - std::floor(origin.z);
	}

	float tMaxX = inf;
	float tMaxY = inf;
	float tMaxZ = inf;
	if (stepX != 0)
	{
		tMaxX = nextX * tDeltaX;
	}
	if (stepY != 0)
	{
		tMaxY = nextY * tDeltaY;
	}
	if (stepZ != 0)
	{
		tMaxZ = nextZ * tDeltaZ;
	}

	float traveled = 0.0f;
	while (traveled <= maxDist)
	{
		if (y >= 0 && y < CHUNK_SIZE_Y && getBlockWorld(x, y, z) != 0)
		{
			hitBlock = glm::ivec3(x, y, z);
			placeBlock = prevCell;
			return true;
		}

		prevCell = glm::ivec3(x, y, z);
		if (tMaxX < tMaxY)
		{
			if (tMaxX < tMaxZ)
			{
				x += stepX;
				traveled = tMaxX;
				tMaxX += tDeltaX;
			}
			else
			{
				z += stepZ;
				traveled = tMaxZ;
				tMaxZ += tDeltaZ;
			}
		}
		else
		{
			if (tMaxY < tMaxZ)
			{
				y += stepY;
				traveled = tMaxY;
				tMaxY += tDeltaY;
			}
			else
			{
				z += stepZ;
				traveled = tMaxZ;
				tMaxZ += tDeltaZ;
			}
		}
	}

	return false;
}

void tryPlaceBlock(const Camera &camera,
				   const BlockInteractionState &state,
				   const std::function<uint32_t(int, int, int)> &getBlockWorld,
				   WorldClient &worldClient)
{
	if (!state.hasWorldFrontier)
	{
		return;
	}

	glm::ivec3 hit;
	glm::ivec3 place;
	if (!raycastPlaceTarget(camera.Position, camera.Front, PLACE_REACH, getBlockWorld, hit, place))
	{
		return;
	}
	if (place.y <= 0 || place.y >= CHUNK_SIZE_Y)
	{
		return;
	}
	if (!usesClassicStreaming(state) &&
		!state.frontier->playableBounds.containsWorldBlock(place.x, place.z))
	{
		return;
	}
	if (getBlockWorld(place.x, place.y, place.z) != 0)
	{
		return;
	}

	worldClient.sendPlaceBlock(place.x, place.y, place.z, static_cast<uint8_t>(state.selectedPaletteIndex - 1));
}

void tryBreakBlock(const Camera &camera,
				   const BlockInteractionState &state,
				   const std::function<uint32_t(int, int, int)> &getBlockWorld,
				   WorldClient &worldClient)
{
	if (!state.hasWorldFrontier)
	{
		return;
	}

	glm::ivec3 hit;
	glm::ivec3 place;
	if (!raycastPlaceTarget(camera.Position, camera.Front, PLACE_REACH, getBlockWorld, hit, place))
	{
		return;
	}
	if (hit.y <= 0 || hit.y >= CHUNK_SIZE_Y)
	{
		return;
	}
	if (!usesClassicStreaming(state) &&
		!state.frontier->playableBounds.containsWorldBlock(hit.x, hit.z))
	{
		return;
	}

	worldClient.sendBreakBlock(hit.x, hit.y, hit.z);
}
