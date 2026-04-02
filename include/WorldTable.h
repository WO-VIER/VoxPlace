#ifndef WORLD_TABLE_H
#define WORLD_TABLE_H

#include <VoxelChunkData.h>

#include <sqlite3.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

enum class WorldTableLoadChunkResult : uint8_t
{
	Loaded = 0,
	Missing = 1,
	Error = 2
};

class WorldTable
{
public:
	WorldTable();
	~WorldTable();

	bool open(const std::string &databasePath, const std::string &generationModeName);
	void close();
	bool isOpen() const;

	WorldTableLoadChunkResult loadChunkResult(int cx,
											  int cz,
											  VoxelChunkData &chunk,
											  std::string *errorMessage = nullptr);
	bool loadChunk(int cx, int cz, VoxelChunkData &chunk);
	bool loadAllChunkKeys(std::vector<int64_t> &outChunkKeys);
	bool saveChunk(const VoxelChunkData &chunk);
	bool saveChunksBatch(const std::vector<VoxelChunkData> &chunks);

	const std::string &lastError() const;
	std::string lastErrorCopy() const;

private:
	sqlite3 *m_db = nullptr;
	sqlite3_stmt *m_loadChunkStatement = nullptr;
	sqlite3_stmt *m_saveChunkStatement = nullptr;
	std::string m_lastError;
	mutable std::mutex m_mutex;

	bool executeStatementNoLock(const char *sql);
	bool prepareStatementNoLock(const char *sql, sqlite3_stmt **statement);
	bool ensureMetaValueNoLock(const std::string &key, const std::string &value);
	bool preparePersistentStatementsNoLock();
	bool beginTransactionNoLock();
	bool commitTransactionNoLock();
	void rollbackTransactionNoLock();
	bool saveChunkUsingPreparedStatementNoLock(
		int64_t key,
		int chunkX,
		int chunkZ,
		uint64_t revision,
		const void *payloadData,
		size_t payloadSize,
		uint64_t nowMs);
	void closeNoLock();
	void setLastErrorFromDatabaseNoLock(const std::string &prefix);
};

#endif
