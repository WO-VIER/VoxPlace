#ifndef PLAYER_TABLE_H
#define PLAYER_TABLE_H

#include <Player.h>

#include <sqlite3.h>

#include <string>

class PlayerTable
{
public:
	PlayerTable();
	~PlayerTable();

	bool open(const std::string &databasePath);
	void close();
	bool isOpen() const;

	bool loadPlayerByUsername(const std::string &username, Player &player);
	bool loadPlayerAuthByUsername(const std::string &username, Player &player, std::string &passwordHash);
	bool createPlayer(const std::string &username, const std::string &passwordHash, Player &player);
	bool loadOrCreatePlayer(const std::string &username,
							const std::string &passwordHashForNewPlayer,
							Player &player,
							std::string &storedPasswordHash,
							bool &createdPlayer);
	bool updatePasswordHash(uint64_t playerId, const std::string &passwordHash);
	bool updateAdminFlag(uint64_t playerId, bool admin);
	bool deletePlayer(uint64_t playerId);
	bool savePlayer(const Player &player);

	const std::string &lastError() const;

private:
	sqlite3 *m_db = nullptr;
	std::string m_lastError;

	bool executeStatement(const char *sql);
	bool prepareStatement(const char *sql, sqlite3_stmt **statement);
	void setLastErrorFromDatabase(const std::string &prefix);
};

#endif
