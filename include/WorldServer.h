#ifndef WORLD_SERVER_H
#define WORLD_SERVER_H

#include <IChunkGenerator.h>
#include <server/core/ServerLaunch.h>
#include <WorldBounds.h>
#include <WorldProtocol.h>
#include <memory>
#include <string>
#include <unordered_map>

class WorldServer
{
public:
	WorldServer(uint16_t port,
				std::unique_ptr<IChunkGenerator> generator,
				WorldGenerationMode generationMode = WorldGenerationMode::ActivityFrontier,
				std::string playerDatabasePath = "voxplace_players.sqlite3",
				std::string worldDatabasePath = "voxplace_world.sqlite3",
				ServerEnvironmentOptions environmentOptions = {});
	~WorldServer();

	bool start();
	int run();
	void stop();
	
private:
	struct Impl;
	Impl *m_impl;
};

void requestWorldServerSignalStop();
void resetWorldServerSignalStop();
bool isWorldServerSignalStopRequested();

#endif
