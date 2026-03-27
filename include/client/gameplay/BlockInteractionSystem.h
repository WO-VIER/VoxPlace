#ifndef CLIENT_WORLD_BLOCK_INTERACTION_SYSTEM_H
#define CLIENT_WORLD_BLOCK_INTERACTION_SYSTEM_H

#include <client/rendering/Camera.h>
#include <WorldBounds.h>
#include <WorldClient.h>

#include <functional>

struct BlockInteractionState
{
	bool hasWorldFrontier = false;
	const WorldFrontier *frontier = nullptr;
	int selectedPaletteIndex = 1;
};

bool raycastPlaceTarget(const glm::vec3 &origin,
						const glm::vec3 &direction,
						float maxDist,
						const std::function<uint32_t(int, int, int)> &getBlockWorld,
						glm::ivec3 &hitBlock,
						glm::ivec3 &placeBlock);

void tryPlaceBlock(const Camera &camera,
				   const BlockInteractionState &state,
				   const std::function<uint32_t(int, int, int)> &getBlockWorld,
				   WorldClient &worldClient);

void tryBreakBlock(const Camera &camera,
				   const BlockInteractionState &state,
				   const std::function<uint32_t(int, int, int)> &getBlockWorld,
				   WorldClient &worldClient);

#endif
