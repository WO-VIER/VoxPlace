#ifndef PLAYER_SESSION_DATA_H
#define PLAYER_SESSION_DATA_H

#include <cstdint>

struct PlayerSessionData
{
	uint64_t playerId = 0;
	uint64_t lastSeenAtMs = 0;
	bool authenticated = false;
};

#endif
