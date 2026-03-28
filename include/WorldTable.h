#ifndef WORLD_TABLE_H
#define WORLD_TABLE_H

#include <VoxelChunkData.h>

#include <sqlite3.h>

#include <mutex>
#include <string>

class WorldTable
{
public:
	WorldTable();
	~WorldTable();

	bool open(const std::string &databasePath, const std::string &generationModeName);
	void close();
	bool isOpen() const;

	bool loadChunk(int cx, int cz, VoxelChunkData &chunk);
	bool saveChunk(const VoxelChunkData &chunk);

	const std::string &lastError() const;

private:
	sqlite3 *m_db = nullptr;
	std::string m_lastError;
	mutable std::mutex m_mutex;

	bool executeStatementNoLock(const char *sql);
	bool prepareStatementNoLock(const char *sql, sqlite3_stmt **statement);
	bool ensureMetaValueNoLock(const std::string &key, const std::string &value);
	void closeNoLock();
	void setLastErrorFromDatabaseNoLock(const std::string &prefix);
};

#endif
