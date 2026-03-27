#ifndef CLIENT_GAMEPLAY_CAMERA_BOUNDS_SYSTEM_H
#define CLIENT_GAMEPLAY_CAMERA_BOUNDS_SYSTEM_H

#include <client/rendering/Camera.h>

#include <WorldBounds.h>

struct CameraChunkCoord
{
	int x = 0;
	int z = 0;
};

const char *cameraHeadingCardinal(const glm::vec3 &front);
CameraChunkCoord cameraChunkCoord(const Camera &camera);
void clampCameraToPlayableWorld(const WorldFrontier &frontier,
								bool hasWorldFrontier,
								bool limitToPlayableWorld,
								Camera &camera);

#endif
