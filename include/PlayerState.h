#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include <glm/vec3.hpp>

#include <cstdint>

constexpr uint64_t PLAYER_DEFAULT_BLOCK_ACTION_COOLDOWN_MS = 2000;

struct PlayerState
{
	glm::vec3 position = glm::vec3(0.0f, 35.0f, 0.0f);
	glm::vec3 lookDirection = glm::vec3(0.0f, 0.0f, -1.0f);
	uint64_t blockActionReadyAtMs = 0;
};

#endif
