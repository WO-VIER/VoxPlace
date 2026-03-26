#ifndef PLAYER_DATA_H
#define PLAYER_DATA_H

#include <PlayerColdData.h>
#include <PlayerHotData.h>

struct PlayerData
{
	PlayerColdData cold;
	PlayerHotData hot;
};

#endif
