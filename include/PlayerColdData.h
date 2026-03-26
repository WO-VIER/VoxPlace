#ifndef PLAYER_COLD_DATA_H
#define PLAYER_COLD_DATA_H

#include <PlayerUsername.h>
#include <cstdint>
#include <string>

struct PlayerColdData
{
	uint64_t playerId = 0;
	std::string username;
	uint16_t skinId = 0;
};

#endif
