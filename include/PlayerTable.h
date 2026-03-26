#ifndef PLAYER_TABLE_H
#define PLAYER_TABLE_H

#include <PlayerData.h>

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

	bool loadPlayerByUsername(const std::string &username, PlayerData &player);
	bool createPlayer(const std::string &username, PlayerData &player);
	bool loadOrCreatePlayer(const std::string &username, PlayerData &player, bool &createdPlayer);
	bool savePlayer(const PlayerData &player);

	const std::string &lastError() const;

private:
	sqlite3 *m_db = nullptr;
	std::string m_lastError;

	bool executeStatement(const char *sql);
	bool prepareStatement(const char *sql, sqlite3_stmt **statement);
	void setLastErrorFromDatabase(const std::string &prefix);
};

#endif
