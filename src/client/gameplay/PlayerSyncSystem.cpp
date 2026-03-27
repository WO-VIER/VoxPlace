#include <client/gameplay/PlayerSyncSystem.h>

#include <chrono>

namespace
{
	constexpr uint64_t PLAYER_MOVE_SYNC_MIN_INTERVAL_MS = 100;
	constexpr uint64_t PLAYER_MOVE_SYNC_MAX_IDLE_INTERVAL_MS = 250;
	constexpr float PLAYER_MOVE_SYNC_POSITION_THRESHOLD = 0.15f;
	constexpr float PLAYER_MOVE_SYNC_LOOK_DOT_THRESHOLD = 0.9993f;

	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}
}

void resetPlayerMovementSyncState(GameState &gameState, const Camera &camera, WorldClient &worldClient)
{
	gameState.movementSync.lastSentPosition = camera.Position;
	gameState.movementSync.lastSentLookDirection = glm::normalize(camera.Front);
	gameState.movementSync.lastSentAtMs = 0;
	worldClient.updateLocalPlayerTransform(
		gameState.movementSync.lastSentPosition,
		gameState.movementSync.lastSentLookDirection);
}

void maybeSendPlayerMovementSync(GameState &gameState, const Camera &camera, WorldClient &worldClient)
{
	if (!worldClient.isConnected())
	{
		return;
	}

	glm::vec3 currentPosition = camera.Position;
	glm::vec3 currentLookDirection = glm::normalize(camera.Front);
	uint64_t nowMs = systemNowMs();

	float positionDeltaSq = glm::dot(
		currentPosition - gameState.movementSync.lastSentPosition,
		currentPosition - gameState.movementSync.lastSentPosition);
	float lookDot = glm::dot(currentLookDirection, gameState.movementSync.lastSentLookDirection);
	if (lookDot < -1.0f)
	{
		lookDot = -1.0f;
	}
	if (lookDot > 1.0f)
	{
		lookDot = 1.0f;
	}

	bool neverSent = gameState.movementSync.lastSentAtMs == 0;
	bool minIntervalElapsed =
		neverSent || (nowMs - gameState.movementSync.lastSentAtMs) >= PLAYER_MOVE_SYNC_MIN_INTERVAL_MS;
	bool idleIntervalElapsed =
		neverSent || (nowMs - gameState.movementSync.lastSentAtMs) >= PLAYER_MOVE_SYNC_MAX_IDLE_INTERVAL_MS;
	bool movedEnough =
		positionDeltaSq >=
		(PLAYER_MOVE_SYNC_POSITION_THRESHOLD * PLAYER_MOVE_SYNC_POSITION_THRESHOLD);
	bool rotatedEnough = lookDot <= PLAYER_MOVE_SYNC_LOOK_DOT_THRESHOLD;

	if (!neverSent &&
		!(idleIntervalElapsed || (minIntervalElapsed && (movedEnough || rotatedEnough))))
	{
		return;
	}

	worldClient.updateLocalPlayerTransform(currentPosition, currentLookDirection);
	worldClient.sendPlayerMoveUpdate(currentPosition, currentLookDirection);
	gameState.movementSync.lastSentPosition = currentPosition;
	gameState.movementSync.lastSentLookDirection = currentLookDirection;
	gameState.movementSync.lastSentAtMs = nowMs;
}
