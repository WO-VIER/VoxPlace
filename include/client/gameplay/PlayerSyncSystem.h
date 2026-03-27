#ifndef CLIENT_WORLD_PLAYER_SYNC_SYSTEM_H
#define CLIENT_WORLD_PLAYER_SYNC_SYSTEM_H

#include <client/rendering/Camera.h>
#include <WorldClient.h>
#include <client/core/GameState.h>

void resetPlayerMovementSyncState(GameState &gameState, const Camera &camera, WorldClient &worldClient);
void maybeSendPlayerMovementSync(GameState &gameState, const Camera &camera, WorldClient &worldClient);

#endif
