#include <PlayerTable.h>

#include <PlayerState.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace
{
	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}
}

PlayerTable::PlayerTable()
{
}

PlayerTable::~PlayerTable()
{
	close();
}

bool PlayerTable::open(const std::string &databasePath)
{
	close();
	m_lastError.clear();

	if (sqlite3_open(databasePath.c_str(), &m_db) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to open player database");
		close();
		return false;
	}

	const char *schemaSql =
		"CREATE TABLE IF NOT EXISTS player_table ("
		" id INTEGER PRIMARY KEY AUTOINCREMENT,"
		" username TEXT NOT NULL UNIQUE,"
		" skin_id INTEGER NOT NULL DEFAULT 0,"
		" position_x REAL NOT NULL DEFAULT 0.0,"
		" position_y REAL NOT NULL DEFAULT 35.0,"
		" position_z REAL NOT NULL DEFAULT 0.0,"
		" block_action_ready_at_ms INTEGER NOT NULL DEFAULT 0,"
		" password_hash TEXT NOT NULL DEFAULT '',"
		" created_at_ms INTEGER NOT NULL DEFAULT 0,"
		" updated_at_ms INTEGER NOT NULL DEFAULT 0"
		");";

	if (!executeStatement(schemaSql))
	{
		return false;
	}

	const char *passwordColumnSql =
		"ALTER TABLE player_table ADD COLUMN password_hash TEXT NOT NULL DEFAULT '';";
	char *errorMessage = nullptr;
	int alterResult = sqlite3_exec(m_db, passwordColumnSql, nullptr, nullptr, &errorMessage);
	if (alterResult != SQLITE_OK && errorMessage != nullptr)
	{
		std::string errorText = errorMessage;
		sqlite3_free(errorMessage);
		if (errorText.find("duplicate column name") == std::string::npos)
		{
			m_lastError = "Failed to ensure password_hash column: " + errorText;
			return false;
		}
	}

	return true;
}

void PlayerTable::close()
{
	if (m_db != nullptr)
	{
		sqlite3_close(m_db);
		m_db = nullptr;
	}
}

bool PlayerTable::isOpen() const
{
	return m_db != nullptr;
}

bool PlayerTable::loadPlayerByUsername(const std::string &username, Player &player)
{
	std::string ignoredPasswordHash;
	return loadPlayerAuthByUsername(username, player, ignoredPasswordHash);
}

bool PlayerTable::loadPlayerAuthByUsername(const std::string &username,
										   Player &player,
										   std::string &passwordHash)
{
	m_lastError.clear();
	if (!isOpen())
	{
		m_lastError = "Player database is not open";
		return false;
	}

	const char *sql =
		"SELECT id, username, skin_id, position_x, position_y, position_z, block_action_ready_at_ms, password_hash "
		"FROM player_table WHERE username = ?1;";

	sqlite3_stmt *statement = nullptr;
	if (!prepareStatement(sql, &statement))
	{
		return false;
	}

	if (sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to bind username for player load");
		sqlite3_finalize(statement);
		return false;
	}

	int stepResult = sqlite3_step(statement);
	if (stepResult == SQLITE_ROW)
	{
		player.profile.playerId = static_cast<uint64_t>(sqlite3_column_int64(statement, 0));
		const unsigned char *storedUsername = sqlite3_column_text(statement, 1);
		if (storedUsername != nullptr)
		{
			player.profile.username = reinterpret_cast<const char *>(storedUsername);
		}
		else
		{
			player.profile.username.clear();
		}
		player.profile.skinId = static_cast<uint16_t>(sqlite3_column_int(statement, 2));
		player.state.position.x = static_cast<float>(sqlite3_column_double(statement, 3));
		player.state.position.y = static_cast<float>(sqlite3_column_double(statement, 4));
		player.state.position.z = static_cast<float>(sqlite3_column_double(statement, 5));
		player.state.blockActionReadyAtMs =
			static_cast<uint64_t>(sqlite3_column_int64(statement, 6));
		const unsigned char *storedHash = sqlite3_column_text(statement, 7);
		if (storedHash != nullptr)
		{
			passwordHash = reinterpret_cast<const char *>(storedHash);
		}
		else
		{
			passwordHash.clear();
		}
		sqlite3_finalize(statement);
		return true;
	}

	if (stepResult == SQLITE_DONE)
	{
		sqlite3_finalize(statement);
		return false;
	}

	if (stepResult != SQLITE_DONE)
	{
		setLastErrorFromDatabase("Failed to read player row");
	}

	sqlite3_finalize(statement);
	return false;
}

bool PlayerTable::createPlayer(const std::string &username,
							   const std::string &passwordHash,
							   Player &player)
{
	m_lastError.clear();
	if (!isOpen())
	{
		m_lastError = "Player database is not open";
		return false;
	}

	const char *sql =
		"INSERT INTO player_table (username, skin_id, position_x, position_y, position_z, block_action_ready_at_ms, password_hash, created_at_ms, updated_at_ms) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);";

	sqlite3_stmt *statement = nullptr;
	if (!prepareStatement(sql, &statement))
	{
		return false;
	}

	player.profile.username = username;
	player.profile.skinId = 0;
	player.state.position.x = 0.0f;
	player.state.position.y = 35.0f;
	player.state.position.z = 0.0f;
	player.state.blockActionReadyAtMs = 0;

	uint64_t nowMs = systemNowMs();

	if (sqlite3_bind_text(statement, 1, player.profile.username.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK
		|| sqlite3_bind_int(statement, 2, static_cast<int>(player.profile.skinId)) != SQLITE_OK
		|| sqlite3_bind_double(statement, 3, static_cast<double>(player.state.position.x)) != SQLITE_OK
		|| sqlite3_bind_double(statement, 4, static_cast<double>(player.state.position.y)) != SQLITE_OK
		|| sqlite3_bind_double(statement, 5, static_cast<double>(player.state.position.z)) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(player.state.blockActionReadyAtMs)) != SQLITE_OK
		|| sqlite3_bind_text(statement, 7, passwordHash.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 8, static_cast<sqlite3_int64>(nowMs)) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 9, static_cast<sqlite3_int64>(nowMs)) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to bind player creation statement");
		sqlite3_finalize(statement);
		return false;
	}

	if (sqlite3_step(statement) != SQLITE_DONE)
	{
		setLastErrorFromDatabase("Failed to insert new player");
		sqlite3_finalize(statement);
		return false;
	}

	player.profile.playerId = static_cast<uint64_t>(sqlite3_last_insert_rowid(m_db));
	sqlite3_finalize(statement);
	return true;
}

bool PlayerTable::loadOrCreatePlayer(const std::string &username,
									 const std::string &passwordHashForNewPlayer,
									 Player &player,
									 std::string &storedPasswordHash,
									 bool &createdPlayer)
{
	createdPlayer = false;
	m_lastError.clear();
	if (loadPlayerAuthByUsername(username, player, storedPasswordHash))
	{
		return true;
	}

	if (!m_lastError.empty())
	{
		return false;
	}

	if (!createPlayer(username, passwordHashForNewPlayer, player))
	{
		return false;
	}

	storedPasswordHash = passwordHashForNewPlayer;
	createdPlayer = true;
	return true;
}

bool PlayerTable::updatePasswordHash(uint64_t playerId, const std::string &passwordHash)
{
	m_lastError.clear();
	if (!isOpen())
	{
		m_lastError = "Player database is not open";
		return false;
	}

	const char *sql =
		"UPDATE player_table SET password_hash = ?1, updated_at_ms = ?2 WHERE id = ?3;";

	sqlite3_stmt *statement = nullptr;
	if (!prepareStatement(sql, &statement))
	{
		return false;
	}

	uint64_t nowMs = systemNowMs();
	if (sqlite3_bind_text(statement, 1, passwordHash.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(nowMs)) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(playerId)) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to bind password hash update");
		sqlite3_finalize(statement);
		return false;
	}

	if (sqlite3_step(statement) != SQLITE_DONE)
	{
		setLastErrorFromDatabase("Failed to update password hash");
		sqlite3_finalize(statement);
		return false;
	}

	sqlite3_finalize(statement);
	return true;
}

bool PlayerTable::deletePlayer(uint64_t playerId)
{
	m_lastError.clear();
	if (!isOpen())
	{
		m_lastError = "Player database is not open";
		return false;
	}
	if (playerId == 0)
	{
		m_lastError = "Cannot delete player with invalid id";
		return false;
	}

	const char *sql = "DELETE FROM player_table WHERE id = ?1;";
	sqlite3_stmt *statement = nullptr;
	if (!prepareStatement(sql, &statement))
	{
		return false;
	}
	if (sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(playerId)) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to bind player delete");
		sqlite3_finalize(statement);
		return false;
	}
	if (sqlite3_step(statement) != SQLITE_DONE)
	{
		setLastErrorFromDatabase("Failed to delete player");
		sqlite3_finalize(statement);
		return false;
	}
	sqlite3_finalize(statement);
	return true;
}

bool PlayerTable::savePlayer(const Player &player)
{
	m_lastError.clear();
	if (!isOpen())
	{
		m_lastError = "Player database is not open";
		return false;
	}
	if (player.profile.playerId == 0)
	{
		m_lastError = "Cannot save player with invalid id";
		return false;
	}

	const char *sql =
		"UPDATE player_table "
		"SET username = ?1, skin_id = ?2, position_x = ?3, position_y = ?4, position_z = ?5, "
		"block_action_ready_at_ms = ?6, updated_at_ms = ?7 "
		"WHERE id = ?8;";

	sqlite3_stmt *statement = nullptr;
	if (!prepareStatement(sql, &statement))
	{
		return false;
	}

	uint64_t nowMs = systemNowMs();

	if (sqlite3_bind_text(statement, 1, player.profile.username.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK
		|| sqlite3_bind_int(statement, 2, static_cast<int>(player.profile.skinId)) != SQLITE_OK
		|| sqlite3_bind_double(statement, 3, static_cast<double>(player.state.position.x)) != SQLITE_OK
		|| sqlite3_bind_double(statement, 4, static_cast<double>(player.state.position.y)) != SQLITE_OK
		|| sqlite3_bind_double(statement, 5, static_cast<double>(player.state.position.z)) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(player.state.blockActionReadyAtMs)) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 7, static_cast<sqlite3_int64>(nowMs)) != SQLITE_OK
		|| sqlite3_bind_int64(statement, 8, static_cast<sqlite3_int64>(player.profile.playerId)) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to bind player save statement");
		sqlite3_finalize(statement);
		return false;
	}

	if (sqlite3_step(statement) != SQLITE_DONE)
	{
		setLastErrorFromDatabase("Failed to update player");
		sqlite3_finalize(statement);
		return false;
	}

	sqlite3_finalize(statement);
	return true;
}

const std::string &PlayerTable::lastError() const
{
	return m_lastError;
}

bool PlayerTable::executeStatement(const char *sql)
{
	char *errorMessage = nullptr;
	if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errorMessage) != SQLITE_OK)
	{
		m_lastError = "Failed to execute SQL statement";
		if (errorMessage != nullptr)
		{
			m_lastError += ": ";
			m_lastError += errorMessage;
			sqlite3_free(errorMessage);
		}
		return false;
	}
	return true;
}

bool PlayerTable::prepareStatement(const char *sql, sqlite3_stmt **statement)
{
	if (sqlite3_prepare_v2(m_db, sql, -1, statement, nullptr) != SQLITE_OK)
	{
		setLastErrorFromDatabase("Failed to prepare SQL statement");
		return false;
	}
	return true;
}

void PlayerTable::setLastErrorFromDatabase(const std::string &prefix)
{
	m_lastError = prefix;
	if (m_db != nullptr)
	{
		const char *databaseMessage = sqlite3_errmsg(m_db);
		if (databaseMessage != nullptr && databaseMessage[0] != '\0')
		{
			m_lastError += ": ";
			m_lastError += databaseMessage;
		}
	}
}
