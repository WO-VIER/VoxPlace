#include <WorldTable.h>

#include <WorldProtocol.h>

#include <zstd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
	constexpr const char *WORLD_STORAGE_FORMAT_VERSION = "1";
	constexpr const char *WORLD_STORAGE_ENCODING = "chunk_snapshot_sections_zstd_v1";
	constexpr int WORLD_STORAGE_ZSTD_LEVEL = 3;

	struct PreparedChunkPayload
	{
		int64_t key = 0;
		int chunkX = 0;
		int chunkZ = 0;
		uint64_t revision = 0;
		uint64_t nowMs = 0;
		std::vector<uint8_t> payload;
	};

	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}

	bool tryDecodeChunkPayload(const void *blob, size_t blobSize, DecodedChunkSnapshot &decoded)
	{
		if (blob == nullptr || blobSize == 0)
		{
			return false;
		}

		unsigned long long decompressedSize =
			ZSTD_getFrameContentSize(blob, blobSize);
		if (decompressedSize == ZSTD_CONTENTSIZE_ERROR ||
			decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN)
		{
			return false;
		}
		if (decompressedSize > static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
		{
			return false;
		}

		std::vector<uint8_t> decompressedBuffer(
			static_cast<size_t>(decompressedSize));
		size_t result = ZSTD_decompress(
			decompressedBuffer.data(),
			decompressedBuffer.size(),
			blob,
			blobSize);
		if (ZSTD_isError(result))
		{
			return false;
		}
		if (result != decompressedBuffer.size())
		{
			return false;
		}

		return decodeChunkSnapshot(
			decompressedBuffer.data(),
			decompressedBuffer.size(),
			decoded);
	}

	bool prepareChunkPayload(
		const VoxelChunkData &chunk,
		PreparedChunkPayload &prepared,
		std::string &error)
	{
		std::vector<uint8_t> encodedChunk = encodeChunkSnapshot(chunk);
		size_t maxCompressedSize = ZSTD_compressBound(encodedChunk.size());
		prepared.payload.resize(maxCompressedSize);
		size_t compressedSize = ZSTD_compress(
			prepared.payload.data(),
			prepared.payload.size(),
			encodedChunk.data(),
			encodedChunk.size(),
			WORLD_STORAGE_ZSTD_LEVEL);
		if (ZSTD_isError(compressedSize))
		{
			error = "Failed to compress world chunk payload: ";
			error += ZSTD_getErrorName(compressedSize);
			return false;
		}
		prepared.payload.resize(compressedSize);
		prepared.key = chunkKey(chunk.chunkX, chunk.chunkZ);
		prepared.chunkX = chunk.chunkX;
		prepared.chunkZ = chunk.chunkZ;
		prepared.revision = chunk.revision;
		prepared.nowMs = systemNowMs();
		return true;
	}
}

WorldTable::WorldTable()
{
}

WorldTable::~WorldTable()
{
	close();
}

bool WorldTable::open(const std::string &databasePath, const std::string &generationModeName)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	closeNoLock();
	m_lastError.clear();

	if (sqlite3_open(databasePath.c_str(), &m_db) != SQLITE_OK)
	{
		setLastErrorFromDatabaseNoLock("Failed to open world database");
		closeNoLock();
		return false;
	}

	const char *metaSchemaSql =
		"CREATE TABLE IF NOT EXISTS world_meta ("
		" key TEXT PRIMARY KEY,"
		" value TEXT NOT NULL"
		");";

	if (!executeStatementNoLock(metaSchemaSql))
	{
		closeNoLock();
		return false;
	}

	const char *chunkSchemaSql =
		"CREATE TABLE IF NOT EXISTS world_chunk_table ("
		" chunk_key INTEGER PRIMARY KEY,"
		" chunk_x INTEGER NOT NULL,"
		" chunk_z INTEGER NOT NULL,"
		" revision INTEGER NOT NULL,"
		" payload BLOB NOT NULL,"
		" updated_at_ms INTEGER NOT NULL"
		");";

	if (!executeStatementNoLock(chunkSchemaSql))
	{
		closeNoLock();
		return false;
	}

	if (!ensureMetaValueNoLock("format_version", WORLD_STORAGE_FORMAT_VERSION) ||
		!ensureMetaValueNoLock("chunk_encoding", WORLD_STORAGE_ENCODING) ||
		!ensureMetaValueNoLock("generation_mode", generationModeName))
	{
		closeNoLock();
		return false;
	}

	executeStatementNoLock("PRAGMA journal_mode=WAL;");
	executeStatementNoLock("PRAGMA synchronous=NORMAL;");
	if (!preparePersistentStatementsNoLock())
	{
		closeNoLock();
		return false;
	}

	return true;
}

void WorldTable::close()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	closeNoLock();
}

void WorldTable::closeNoLock()
{
	if (m_db != nullptr)
	{
		if (m_loadChunkStatement != nullptr)
		{
			sqlite3_finalize(m_loadChunkStatement);
			m_loadChunkStatement = nullptr;
		}
		if (m_saveChunkStatement != nullptr)
		{
			sqlite3_finalize(m_saveChunkStatement);
			m_saveChunkStatement = nullptr;
		}
		sqlite3_close(m_db);
		m_db = nullptr;
	}
}

bool WorldTable::isOpen() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_db != nullptr;
}

WorldTableLoadChunkResult WorldTable::loadChunkResult(int cx,
													  int cz,
													  VoxelChunkData &chunk,
													  std::string *errorMessage)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_lastError.clear();
	if (errorMessage != nullptr)
	{
		errorMessage->clear();
	}
	if (m_db == nullptr)
	{
		m_lastError = "World database is not open";
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}

	if (m_loadChunkStatement == nullptr)
	{
		m_lastError = "World load statement is not prepared";
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}
	sqlite3_stmt *statement = m_loadChunkStatement;
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);

	int64_t key = chunkKey(cx, cz);
	if (sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(key)) != SQLITE_OK)
	{
		setLastErrorFromDatabaseNoLock("Failed to bind chunk key for world load");
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}

	int stepResult = sqlite3_step(statement);
	if (stepResult == SQLITE_DONE)
	{
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		return WorldTableLoadChunkResult::Missing;
	}

	if (stepResult != SQLITE_ROW)
	{
		setLastErrorFromDatabaseNoLock("Failed to read world chunk row");
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}

	const void *blob = sqlite3_column_blob(statement, 0);
	int blobSize = sqlite3_column_bytes(statement, 0);
	if (blob == nullptr || blobSize <= 0)
	{
		m_lastError = "World chunk payload is empty";
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}

	DecodedChunkSnapshot decoded;
	if (!tryDecodeChunkPayload(blob, static_cast<size_t>(blobSize), decoded))
	{
		m_lastError = "Failed to decode stored world chunk payload";
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);

	if (decoded.chunk.chunkX != cx || decoded.chunk.chunkZ != cz)
	{
		m_lastError = "Stored world chunk coordinates do not match requested chunk";
		if (errorMessage != nullptr)
		{
			*errorMessage = m_lastError;
		}
		return WorldTableLoadChunkResult::Error;
	}

	chunk = std::move(decoded.chunk);
	return WorldTableLoadChunkResult::Loaded;
}

bool WorldTable::loadChunk(int cx, int cz, VoxelChunkData &chunk)
{
	return loadChunkResult(cx, cz, chunk) == WorldTableLoadChunkResult::Loaded;
}

bool WorldTable::loadAllChunkKeys(std::vector<int64_t> &outChunkKeys)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_lastError.clear();
	outChunkKeys.clear();
	if (m_db == nullptr)
	{
		m_lastError = "World database is not open";
		return false;
	}

	const char *sql =
		"SELECT chunk_key FROM world_chunk_table;";
	sqlite3_stmt *statement = nullptr;
	if (!prepareStatementNoLock(sql, &statement))
	{
		return false;
	}

	while (true)
	{
		int stepResult = sqlite3_step(statement);
		if (stepResult == SQLITE_DONE)
		{
			break;
		}
		if (stepResult != SQLITE_ROW)
		{
			setLastErrorFromDatabaseNoLock("Failed to iterate world chunk keys");
			sqlite3_finalize(statement);
			outChunkKeys.clear();
			return false;
		}

		int64_t key = static_cast<int64_t>(sqlite3_column_int64(statement, 0));
		outChunkKeys.push_back(key);
	}

	sqlite3_finalize(statement);
	return true;
}

bool WorldTable::saveChunk(const VoxelChunkData &chunk)
{
	std::vector<VoxelChunkData> chunks;
	chunks.push_back(chunk);
	return saveChunksBatch(chunks);
}

bool WorldTable::saveChunksBatch(const std::vector<VoxelChunkData> &chunks)
{
	if (chunks.empty())
	{
		return true;
	}

	std::vector<PreparedChunkPayload> preparedChunks;
	preparedChunks.reserve(chunks.size());
	for (const VoxelChunkData &chunk : chunks)
	{
		PreparedChunkPayload prepared;
		std::string prepareError;
		if (!prepareChunkPayload(chunk, prepared, prepareError))
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = std::move(prepareError);
			return false;
		}
		preparedChunks.push_back(std::move(prepared));
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_lastError.clear();
	if (m_db == nullptr)
	{
		m_lastError = "World database is not open";
		return false;
	}
	if (!beginTransactionNoLock())
	{
		return false;
	}
	for (const PreparedChunkPayload &prepared : preparedChunks)
	{
		if (!saveChunkUsingPreparedStatementNoLock(
				prepared.key,
				prepared.chunkX,
				prepared.chunkZ,
				prepared.revision,
				prepared.payload.data(),
				prepared.payload.size(),
				prepared.nowMs))
		{
			rollbackTransactionNoLock();
			return false;
		}
	}
	if (!commitTransactionNoLock())
	{
		rollbackTransactionNoLock();
		return false;
	}
	return true;
}

bool WorldTable::preparePersistentStatementsNoLock()
{
	const char *loadSql =
		"SELECT payload FROM world_chunk_table WHERE chunk_key = ?1;";
	if (!prepareStatementNoLock(loadSql, &m_loadChunkStatement))
	{
		return false;
	}

	const char *saveSql =
		"REPLACE INTO world_chunk_table "
		"(chunk_key, chunk_x, chunk_z, revision, payload, updated_at_ms) "
		"VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
	if (!prepareStatementNoLock(saveSql, &m_saveChunkStatement))
	{
		return false;
	}

	return true;
}

bool WorldTable::beginTransactionNoLock()
{
	return executeStatementNoLock("BEGIN IMMEDIATE TRANSACTION;");
}

bool WorldTable::commitTransactionNoLock()
{
	return executeStatementNoLock("COMMIT;");
}

void WorldTable::rollbackTransactionNoLock()
{
	(void)executeStatementNoLock("ROLLBACK;");
}

bool WorldTable::saveChunkUsingPreparedStatementNoLock(
	int64_t key,
	int chunkX,
	int chunkZ,
	uint64_t revision,
	const void *payloadData,
	size_t payloadSize,
	uint64_t nowMs)
{
	if (m_saveChunkStatement == nullptr)
	{
		m_lastError = "World save statement is not prepared";
		return false;
	}

	sqlite3_stmt *statement = m_saveChunkStatement;
	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);

	if (sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(key)) != SQLITE_OK ||
		sqlite3_bind_int(statement, 2, chunkX) != SQLITE_OK ||
		sqlite3_bind_int(statement, 3, chunkZ) != SQLITE_OK ||
		sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(revision)) != SQLITE_OK ||
		sqlite3_bind_blob(statement, 5, payloadData, static_cast<int>(payloadSize), SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(nowMs)) != SQLITE_OK)
	{
		setLastErrorFromDatabaseNoLock("Failed to bind world chunk save statement");
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		return false;
	}

	if (sqlite3_step(statement) != SQLITE_DONE)
	{
		setLastErrorFromDatabaseNoLock("Failed to save world chunk");
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
		return false;
	}

	sqlite3_reset(statement);
	sqlite3_clear_bindings(statement);
	return true;
}

const std::string &WorldTable::lastError() const
{
	return m_lastError;
}

std::string WorldTable::lastErrorCopy() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastError;
}

bool WorldTable::executeStatementNoLock(const char *sql)
{
	char *errorMessage = nullptr;
	if (sqlite3_exec(m_db, sql, nullptr, nullptr, &errorMessage) != SQLITE_OK)
	{
		if (errorMessage != nullptr)
		{
			m_lastError = errorMessage;
			sqlite3_free(errorMessage);
		}
		else
		{
			setLastErrorFromDatabaseNoLock("Failed to execute world database statement");
		}
		return false;
	}
	return true;
}

bool WorldTable::prepareStatementNoLock(const char *sql, sqlite3_stmt **statement)
{
	if (sqlite3_prepare_v2(m_db, sql, -1, statement, nullptr) != SQLITE_OK)
	{
		setLastErrorFromDatabaseNoLock("Failed to prepare world database statement");
		return false;
	}
	return true;
}

bool WorldTable::ensureMetaValueNoLock(const std::string &key, const std::string &value)
{
	const char *selectSql =
		"SELECT value FROM world_meta WHERE key = ?1;";

	sqlite3_stmt *selectStatement = nullptr;
	if (!prepareStatementNoLock(selectSql, &selectStatement))
	{
		return false;
	}

	if (sqlite3_bind_text(selectStatement, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
	{
		setLastErrorFromDatabaseNoLock("Failed to bind world meta key");
		sqlite3_finalize(selectStatement);
		return false;
	}

	int stepResult = sqlite3_step(selectStatement);
	if (stepResult == SQLITE_ROW)
	{
		const unsigned char *storedValue = sqlite3_column_text(selectStatement, 0);
		std::string existingValue;
		if (storedValue != nullptr)
		{
			existingValue = reinterpret_cast<const char *>(storedValue);
		}
		sqlite3_finalize(selectStatement);

		if (existingValue != value)
		{
			m_lastError = "World database meta mismatch for key '" + key +
				"': expected '" + value + "', found '" + existingValue + "'";
			return false;
		}
		return true;
	}

	if (stepResult != SQLITE_DONE)
	{
		setLastErrorFromDatabaseNoLock("Failed to read world meta value");
		sqlite3_finalize(selectStatement);
		return false;
	}

	sqlite3_finalize(selectStatement);

	const char *insertSql =
		"INSERT INTO world_meta (key, value) VALUES (?1, ?2);";

	sqlite3_stmt *insertStatement = nullptr;
	if (!prepareStatementNoLock(insertSql, &insertStatement))
	{
		return false;
	}

	if (sqlite3_bind_text(insertStatement, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_text(insertStatement, 2, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
	{
		setLastErrorFromDatabaseNoLock("Failed to bind world meta insert");
		sqlite3_finalize(insertStatement);
		return false;
	}

	if (sqlite3_step(insertStatement) != SQLITE_DONE)
	{
		setLastErrorFromDatabaseNoLock("Failed to write world meta value");
		sqlite3_finalize(insertStatement);
		return false;
	}

	sqlite3_finalize(insertStatement);
	return true;
}
void WorldTable::setLastErrorFromDatabaseNoLock(const std::string &prefix)
{
	if (m_db != nullptr)
	{
		const char *databaseMessage = sqlite3_errmsg(m_db);
		if (databaseMessage != nullptr)
		{
			m_lastError = prefix + ": " + databaseMessage;
			return;
		}
	}
	m_lastError = prefix;
}
