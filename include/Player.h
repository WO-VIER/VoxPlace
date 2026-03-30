#ifndef PLAYER_H
#define PLAYER_H

#include <PlayerProfile.h>
#include <PlayerState.h>

struct Player
{
	PlayerProfile profile;
	PlayerState state;
};

#endif
