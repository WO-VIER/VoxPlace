#ifndef PLAYER_PROFILE_H
#define PLAYER_PROFILE_H

#include <PlayerUsername.h>
#include <cstdint>
#include <string>

struct PlayerProfile
{
	uint64_t playerId = 0;
	std::string username;
	uint16_t skinId = 0;
	bool admin = false;
};

#endif
