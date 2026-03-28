#include <TerrainGenerator.h>
#include <VoxelChunkData.h>
#include <WorldProtocol.h>
#include <WorldTable.h>

#include <sqlite3.h>
#include <zstd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
	constexpr int BENCH_ZSTD_LEVEL = 3;

	struct ChunkSnapshotRleRun
	{
		uint16_t count = 0;
		uint32_t value = 0;
	};

	struct TimerResult
	{
		double totalMs = 0.0;
		double perChunkMs = 0.0;
	};

	struct SizeResult
	{
		size_t totalBytes = 0;
		double averageBytes = 0.0;
		double ratioVsRaw = 0.0;
	};

	struct FileChunkHeader
	{
		char magic[8] = {'V', 'P', 'W', 'B', 'E', 'N', 'C', 'H'};
		uint32_t version = 1;
		uint32_t entryCount = 0;
	};

	struct FileChunkIndexEntry
	{
		int64_t chunkKey = 0;
		uint64_t offset = 0;
		uint32_t size = 0;
		uint32_t reserved = 0;
	};

	template <typename T>
	void appendValue(std::vector<uint8_t> &buffer, const T &value)
	{
		size_t start = buffer.size();
		buffer.resize(start + sizeof(T));
		std::memcpy(buffer.data() + start, &value, sizeof(T));
	}

	double rawChunkBytes()
	{
		return static_cast<double>(sizeof(uint32_t) * CHUNK_BLOCK_COUNT);
	}

	SizeResult buildSizeResultWithRaw(size_t totalBytes, size_t itemCount)
	{
		SizeResult result;
		result.totalBytes = totalBytes;
		if (itemCount > 0)
		{
			result.averageBytes = static_cast<double>(totalBytes) /
				static_cast<double>(itemCount);
		}

		double rawBytes = rawChunkBytes();
		if (rawBytes > 0.0)
		{
			result.ratioVsRaw = result.averageBytes / rawBytes;
		}
		return result;
	}

	void printTimer(const char *label, const TimerResult &result)
	{
		std::cout << label << ": total=" << result.totalMs
				  << " ms, per_chunk=" << result.perChunkMs
				  << " ms" << std::endl;
	}

	void printSize(const char *label, const SizeResult &result)
	{
		std::cout << label << ": avg_bytes=" << result.averageBytes
				  << ", ratio_vs_raw=" << result.ratioVsRaw
				  << std::endl;
	}

	std::vector<std::pair<int, int>> buildCoords(size_t chunkCount)
	{
		std::vector<std::pair<int, int>> coords;
		coords.reserve(chunkCount);
		int width = static_cast<int>(std::ceil(std::sqrt(
			static_cast<double>(chunkCount))));
		int baseX = -width / 2;
		int baseZ = -width / 2;
		for (size_t index = 0; index < chunkCount; index++)
		{
			int x = static_cast<int>(index % static_cast<size_t>(width));
			int z = static_cast<int>(index / static_cast<size_t>(width));
			coords.push_back({baseX + x, baseZ + z});
		}
		return coords;
	}

	std::vector<int64_t> buildFlyThroughKeys(
		const std::vector<std::pair<int, int>> &coords)
	{
		if (coords.empty())
		{
			return {};
		}

		int minX = coords.front().first;
		int maxX = coords.front().first;
		int minZ = coords.front().second;
		int maxZ = coords.front().second;
		std::unordered_set<int64_t> presentKeys;
		presentKeys.reserve(coords.size());

		for (const auto &[cx, cz] : coords)
		{
			minX = std::min(minX, cx);
			maxX = std::max(maxX, cx);
			minZ = std::min(minZ, cz);
			maxZ = std::max(maxZ, cz);
			presentKeys.insert(chunkKey(cx, cz));
		}

		std::vector<int64_t> order;
		order.reserve(coords.size());
		for (int z = minZ; z <= maxZ; z++)
		{
			bool leftToRight = ((z - minZ) % 2) == 0;
			if (leftToRight)
			{
				for (int x = minX; x <= maxX; x++)
				{
					int64_t key = chunkKey(x, z);
					if (presentKeys.find(key) != presentKeys.end())
					{
						order.push_back(key);
					}
				}
			}
			else
			{
				for (int x = maxX; x >= minX; x--)
				{
					int64_t key = chunkKey(x, z);
					if (presentKeys.find(key) != presentKeys.end())
					{
						order.push_back(key);
					}
				}
			}
		}

		return order;
	}

	std::vector<uint8_t> encodeChunkSnapshotRleBench(const VoxelChunkData &chunk)
	{
		std::vector<ChunkSnapshotRleRun> runs;
		runs.reserve(CHUNK_BLOCK_COUNT);

		const uint32_t *values = &chunk.blocks[0][0][0];
		size_t index = 0;
		while (index < CHUNK_BLOCK_COUNT)
		{
			uint32_t value = values[index];
			uint16_t count = 1;
			while (index + count < CHUNK_BLOCK_COUNT &&
				   values[index + count] == value &&
				   count < std::numeric_limits<uint16_t>::max())
			{
				count++;
			}
			runs.push_back(ChunkSnapshotRleRun{count, value});
			index += count;
		}

		std::vector<uint8_t> buffer;
		buffer.reserve(
			sizeof(PacketType) +
			sizeof(chunk.chunkX) +
			sizeof(chunk.chunkZ) +
			sizeof(chunk.revision) +
			sizeof(uint32_t) +
			runs.size() * sizeof(ChunkSnapshotRleRun));
		appendValue(buffer, PacketType::ChunkSnapshotRle);
		appendValue(buffer, chunk.chunkX);
		appendValue(buffer, chunk.chunkZ);
		appendValue(buffer, chunk.revision);
		uint32_t runCount = static_cast<uint32_t>(runs.size());
		appendValue(buffer, runCount);
		for (const ChunkSnapshotRleRun &run : runs)
		{
			appendValue(buffer, run);
		}
		return buffer;
	}

	bool compressZstdPayload(
		const uint8_t *data,
		size_t size,
		int compressionLevel,
		std::vector<uint8_t> &payload)
	{
		size_t maxCompressedSize = ZSTD_compressBound(size);
		payload.resize(maxCompressedSize);
		size_t compressedSize = ZSTD_compress(
			payload.data(),
			payload.size(),
			data,
			size,
			compressionLevel);
		if (ZSTD_isError(compressedSize))
		{
			return false;
		}
		payload.resize(compressedSize);
		return true;
	}

	bool decodeCompressedChunkPayload(
		const uint8_t *payloadData,
		size_t payloadSize,
		DecodedChunkSnapshot &decoded)
	{
		unsigned long long decompressedSize =
			ZSTD_getFrameContentSize(payloadData, payloadSize);
		if (decompressedSize == ZSTD_CONTENTSIZE_ERROR ||
			decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN)
		{
			return false;
		}
		if (decompressedSize >
			static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
		{
			return false;
		}

		std::vector<uint8_t> decompressedBuffer(
			static_cast<size_t>(decompressedSize));
		size_t result = ZSTD_decompress(
			decompressedBuffer.data(),
			decompressedBuffer.size(),
			payloadData,
			payloadSize);
		if (ZSTD_isError(result) || result != decompressedBuffer.size())
		{
			return false;
		}

		return decodeChunkSnapshot(
			decompressedBuffer.data(),
			decompressedBuffer.size(),
			decoded);
	}

	bool writeIndexedWorldFile(
		const std::filesystem::path &path,
		const std::vector<int64_t> &keys,
		const std::vector<std::vector<uint8_t>> &payloads)
	{
		if (keys.size() != payloads.size())
		{
			return false;
		}

		FileChunkHeader header;
		header.entryCount = static_cast<uint32_t>(keys.size());

		std::vector<FileChunkIndexEntry> indexEntries(keys.size());
		uint64_t currentOffset =
			static_cast<uint64_t>(sizeof(FileChunkHeader)) +
			static_cast<uint64_t>(sizeof(FileChunkIndexEntry) * indexEntries.size());

		for (size_t index = 0; index < keys.size(); index++)
		{
			indexEntries[index].chunkKey = keys[index];
			indexEntries[index].offset = currentOffset;
			indexEntries[index].size = static_cast<uint32_t>(payloads[index].size());
			currentOffset += static_cast<uint64_t>(payloads[index].size());
		}

		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output.is_open())
		{
			return false;
		}

		output.write(reinterpret_cast<const char *>(&header), sizeof(header));
		output.write(
			reinterpret_cast<const char *>(indexEntries.data()),
			static_cast<std::streamsize>(
				sizeof(FileChunkIndexEntry) * indexEntries.size()));
		for (const std::vector<uint8_t> &payload : payloads)
		{
			output.write(
				reinterpret_cast<const char *>(payload.data()),
				static_cast<std::streamsize>(payload.size()));
		}

		return output.good();
	}

	bool readIndexedWorldFile(
		const std::filesystem::path &path,
		std::ifstream &input,
		std::unordered_map<int64_t, FileChunkIndexEntry> &index)
	{
		input = std::ifstream(path, std::ios::binary);
		if (!input.is_open())
		{
			return false;
		}

		FileChunkHeader header;
		input.read(reinterpret_cast<char *>(&header), sizeof(header));
		if (!input.good())
		{
			return false;
		}

		const char expectedMagic[8] = {'V', 'P', 'W', 'B', 'E', 'N', 'C', 'H'};
		if (std::memcmp(header.magic, expectedMagic, sizeof(expectedMagic)) != 0 ||
			header.version != 1)
		{
			return false;
		}

		std::vector<FileChunkIndexEntry> entries(header.entryCount);
		input.read(
			reinterpret_cast<char *>(entries.data()),
			static_cast<std::streamsize>(
				sizeof(FileChunkIndexEntry) * entries.size()));
		if (!input.good())
		{
			return false;
		}

		index.clear();
		index.reserve(entries.size());
		for (const FileChunkIndexEntry &entry : entries)
		{
			index[entry.chunkKey] = entry;
		}
		return true;
	}

	bool loadChunkFromIndexedWorldFile(
		std::ifstream &input,
		const std::unordered_map<int64_t, FileChunkIndexEntry> &index,
		int64_t key,
		VoxelChunkData &chunk)
	{
		auto found = index.find(key);
		if (found == index.end())
		{
			return false;
		}

		const FileChunkIndexEntry &entry = found->second;
		std::vector<uint8_t> payload(entry.size);
		input.clear();
		input.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
		input.read(
			reinterpret_cast<char *>(payload.data()),
			static_cast<std::streamsize>(payload.size()));
		if (!input.good())
		{
			return false;
		}

		DecodedChunkSnapshot decoded;
		if (!decodeCompressedChunkPayload(payload.data(), payload.size(), decoded))
		{
			return false;
		}

		chunk = std::move(decoded.chunk);
		return true;
	}

	size_t fileSizeOrZero(const std::filesystem::path &path)
	{
		std::error_code error;
		uintmax_t size = std::filesystem::file_size(path, error);
		if (error)
		{
			return 0;
		}
		return static_cast<size_t>(size);
	}

	bool executeSql(sqlite3 *db, const char *sql)
	{
		char *errorMessage = nullptr;
		if (sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage) != SQLITE_OK)
		{
			if (errorMessage != nullptr)
			{
				std::cerr << "SQLite error: " << errorMessage << std::endl;
				sqlite3_free(errorMessage);
			}
			return false;
		}
		return true;
	}

	bool createWorldChunkSchema(sqlite3 *db)
	{
		return executeSql(
				   db,
				   "PRAGMA journal_mode=WAL;") &&
			executeSql(
				   db,
				   "PRAGMA synchronous=NORMAL;") &&
			executeSql(
				   db,
				   "CREATE TABLE IF NOT EXISTS world_meta ("
				   " key TEXT PRIMARY KEY,"
				   " value TEXT NOT NULL"
				   ");") &&
			executeSql(
				   db,
				   "CREATE TABLE IF NOT EXISTS world_chunk_table ("
				   " chunk_key INTEGER PRIMARY KEY,"
				   " chunk_x INTEGER NOT NULL,"
				   " chunk_z INTEGER NOT NULL,"
				   " revision INTEGER NOT NULL,"
				   " payload BLOB NOT NULL,"
				   " updated_at_ms INTEGER NOT NULL"
				   ");");
	}

	bool writeSqliteBatch(
		const std::filesystem::path &databasePath,
		const std::vector<VoxelChunkData> &chunks,
		const std::vector<std::vector<uint8_t>> &payloads)
	{
		if (chunks.size() != payloads.size())
		{
			return false;
		}

		sqlite3 *db = nullptr;
		if (sqlite3_open(databasePath.c_str(), &db) != SQLITE_OK)
		{
			if (db != nullptr)
			{
				sqlite3_close(db);
			}
			return false;
		}

		bool ok = createWorldChunkSchema(db) &&
			executeSql(db, "BEGIN IMMEDIATE TRANSACTION;");
		sqlite3_stmt *statement = nullptr;
		if (ok)
		{
			const char *sql =
				"REPLACE INTO world_chunk_table "
				"(chunk_key, chunk_x, chunk_z, revision, payload, updated_at_ms) "
				"VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
			ok = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK;
		}

		uint64_t nowMs = 0;
		if (ok)
		{
			nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch())
					.count());
			for (size_t index = 0; index < chunks.size() && ok; index++)
			{
				const VoxelChunkData &chunk = chunks[index];
				const std::vector<uint8_t> &payload = payloads[index];
				int64_t key = chunkKey(chunk.chunkX, chunk.chunkZ);

				ok =
					sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(key)) == SQLITE_OK &&
					sqlite3_bind_int(statement, 2, chunk.chunkX) == SQLITE_OK &&
					sqlite3_bind_int(statement, 3, chunk.chunkZ) == SQLITE_OK &&
					sqlite3_bind_int64(statement, 4, static_cast<sqlite3_int64>(chunk.revision)) == SQLITE_OK &&
					sqlite3_bind_blob(statement, 5, payload.data(), static_cast<int>(payload.size()), SQLITE_TRANSIENT) == SQLITE_OK &&
					sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(nowMs)) == SQLITE_OK &&
					sqlite3_step(statement) == SQLITE_DONE &&
					sqlite3_reset(statement) == SQLITE_OK &&
					sqlite3_clear_bindings(statement) == SQLITE_OK;
			}
		}

		if (statement != nullptr)
		{
			sqlite3_finalize(statement);
			statement = nullptr;
		}

		if (ok)
		{
			ok = executeSql(db, "COMMIT;");
		}
		else
		{
			executeSql(db, "ROLLBACK;");
		}

		sqlite3_close(db);
		return ok;
	}
}

int main(int argc, char **argv)
{
	size_t chunkCount = 512;
	if (argc >= 2)
	{
		chunkCount = static_cast<size_t>(std::strtoull(argv[1], nullptr, 10));
		if (chunkCount == 0)
		{
			std::cerr << "Chunk count must be > 0" << std::endl;
			return 1;
		}
	}

	std::cout << "World storage benchmark" << std::endl;
	std::cout << "Chunk count: " << chunkCount << std::endl;
	std::cout << "Raw chunk bytes: " << static_cast<size_t>(rawChunkBytes()) << std::endl;

	std::vector<std::pair<int, int>> coords = buildCoords(chunkCount);
	std::vector<int64_t> chunkKeys;
	chunkKeys.reserve(coords.size());
	std::unordered_map<int64_t, std::pair<int, int>> coordByKey;
	coordByKey.reserve(coords.size());
	for (const auto &[cx, cz] : coords)
	{
		int64_t key = chunkKey(cx, cz);
		chunkKeys.push_back(key);
		coordByKey[key] = {cx, cz};
	}
	std::vector<int64_t> flyThroughKeys = buildFlyThroughKeys(coords);
	std::vector<int64_t> reverseFlyThroughKeys = flyThroughKeys;
	std::reverse(reverseFlyThroughKeys.begin(), reverseFlyThroughKeys.end());

	TerrainGenerator generator(42);
	std::vector<VoxelChunkData> chunks;
	chunks.reserve(chunkCount);
	{
		auto start = std::chrono::steady_clock::now();
		for (const auto &[cx, cz] : coords)
		{
			VoxelChunkData chunk(cx, cz);
			generator.fillChunk(chunk);
			chunks.push_back(std::move(chunk));
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult generation;
		generation.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		generation.perChunkMs = generation.totalMs / static_cast<double>(chunkCount);
		printTimer("generate", generation);
	}

	std::vector<std::vector<uint8_t>> sectionPayloads;
	sectionPayloads.reserve(chunkCount);
	{
		auto start = std::chrono::steady_clock::now();
		size_t totalBytes = 0;
		for (const VoxelChunkData &chunk : chunks)
		{
			std::vector<uint8_t> payload = encodeChunkSnapshot(chunk);
			totalBytes += payload.size();
			sectionPayloads.push_back(std::move(payload));
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult encodeSections;
		encodeSections.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		encodeSections.perChunkMs =
			encodeSections.totalMs / static_cast<double>(chunkCount);
		printTimer("encode_sections", encodeSections);
		printSize("sections_size", buildSizeResultWithRaw(totalBytes, chunkCount));
	}

	{
		auto start = std::chrono::steady_clock::now();
		for (size_t index = 0; index < chunkCount; index++)
		{
			DecodedChunkSnapshot decoded;
			if (!decodeChunkSnapshot(
					sectionPayloads[index].data(),
					sectionPayloads[index].size(),
					decoded))
			{
				std::cerr << "Failed to decode section payload for chunk "
						  << index << std::endl;
				return 1;
			}
			if (std::memcmp(
					decoded.chunk.blocks,
					chunks[index].blocks,
					sizeof(chunks[index].blocks)) != 0)
			{
				std::cerr << "Decoded section payload mismatch for chunk "
						  << index << std::endl;
				return 1;
			}
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult decodeSections;
		decodeSections.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		decodeSections.perChunkMs =
			decodeSections.totalMs / static_cast<double>(chunkCount);
		printTimer("decode_sections", decodeSections);
	}

	std::vector<std::vector<uint8_t>> rlePayloads;
	rlePayloads.reserve(chunkCount);
	{
		auto start = std::chrono::steady_clock::now();
		size_t totalBytes = 0;
		for (const VoxelChunkData &chunk : chunks)
		{
			std::vector<uint8_t> payload = encodeChunkSnapshotRleBench(chunk);
			totalBytes += payload.size();
			rlePayloads.push_back(std::move(payload));
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult encodeRle;
		encodeRle.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		encodeRle.perChunkMs = encodeRle.totalMs / static_cast<double>(chunkCount);
		printTimer("encode_rle", encodeRle);
		printSize("rle_size", buildSizeResultWithRaw(totalBytes, chunkCount));
	}

	{
		auto start = std::chrono::steady_clock::now();
		for (size_t index = 0; index < chunkCount; index++)
		{
			DecodedChunkSnapshot decoded;
			if (!decodeChunkSnapshot(
					rlePayloads[index].data(),
					rlePayloads[index].size(),
					decoded))
			{
				std::cerr << "Failed to decode RLE payload for chunk "
						  << index << std::endl;
				return 1;
			}
			if (std::memcmp(
					decoded.chunk.blocks,
					chunks[index].blocks,
					sizeof(chunks[index].blocks)) != 0)
			{
				std::cerr << "Decoded RLE payload mismatch for chunk "
						  << index << std::endl;
				return 1;
			}
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult decodeRle;
		decodeRle.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		decodeRle.perChunkMs =
			decodeRle.totalMs / static_cast<double>(chunkCount);
		printTimer("decode_rle", decodeRle);
	}

	std::vector<std::vector<uint8_t>> zstdRawPayloads;
	zstdRawPayloads.reserve(chunkCount);
	{
		auto start = std::chrono::steady_clock::now();
		size_t totalBytes = 0;
		for (const VoxelChunkData &chunk : chunks)
		{
			std::vector<uint8_t> payload;
			if (!compressZstdPayload(
					reinterpret_cast<const uint8_t *>(&chunk.blocks[0][0][0]),
					sizeof(chunk.blocks),
					BENCH_ZSTD_LEVEL,
					payload))
			{
				std::cerr << "Failed to compress raw chunk with ZSTD" << std::endl;
				return 1;
			}
			totalBytes += payload.size();
			zstdRawPayloads.push_back(std::move(payload));
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult encodeZstdRaw;
		encodeZstdRaw.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		encodeZstdRaw.perChunkMs =
			encodeZstdRaw.totalMs / static_cast<double>(chunkCount);
		printTimer("encode_zstd_raw_lvl3", encodeZstdRaw);
		printSize("zstd_raw_size", buildSizeResultWithRaw(totalBytes, chunkCount));
	}

	{
		auto start = std::chrono::steady_clock::now();
		std::vector<uint32_t> scratch(CHUNK_BLOCK_COUNT);
		for (size_t index = 0; index < chunkCount; index++)
		{
			size_t result = ZSTD_decompress(
				scratch.data(),
				sizeof(chunks[index].blocks),
				zstdRawPayloads[index].data(),
				zstdRawPayloads[index].size());
			if (ZSTD_isError(result))
			{
				std::cerr << "Failed to decode raw ZSTD chunk" << std::endl;
				return 1;
			}
			if (result != sizeof(chunks[index].blocks) ||
				std::memcmp(
					scratch.data(),
					chunks[index].blocks,
					sizeof(chunks[index].blocks)) != 0)
			{
				std::cerr << "Decoded raw ZSTD payload mismatch for chunk "
						  << index << std::endl;
				return 1;
			}
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult decodeZstdRaw;
		decodeZstdRaw.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		decodeZstdRaw.perChunkMs =
			decodeZstdRaw.totalMs / static_cast<double>(chunkCount);
		printTimer("decode_zstd_raw_lvl3", decodeZstdRaw);
	}

	std::vector<std::vector<uint8_t>> zstdSectionPayloads;
	zstdSectionPayloads.reserve(chunkCount);
	{
		auto start = std::chrono::steady_clock::now();
		size_t totalBytes = 0;
		for (const std::vector<uint8_t> &sectionPayload : sectionPayloads)
		{
			std::vector<uint8_t> payload;
			if (!compressZstdPayload(
					sectionPayload.data(),
					sectionPayload.size(),
					BENCH_ZSTD_LEVEL,
					payload))
			{
				std::cerr << "Failed to compress section payload with ZSTD"
						  << std::endl;
				return 1;
			}
			totalBytes += payload.size();
			zstdSectionPayloads.push_back(std::move(payload));
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult encodeZstdSections;
		encodeZstdSections.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		encodeZstdSections.perChunkMs =
			encodeZstdSections.totalMs / static_cast<double>(chunkCount);
		printTimer("encode_zstd_sections_lvl3", encodeZstdSections);
		printSize("zstd_sections_size", buildSizeResultWithRaw(totalBytes, chunkCount));
	}

	{
		auto start = std::chrono::steady_clock::now();
		for (size_t index = 0; index < chunkCount; index++)
		{
			DecodedChunkSnapshot decoded;
			if (!decodeCompressedChunkPayload(
					zstdSectionPayloads[index].data(),
					zstdSectionPayloads[index].size(),
					decoded))
			{
				std::cerr << "Failed to decode ZSTD section payload for chunk "
						  << index << std::endl;
				return 1;
			}
			if (std::memcmp(
					decoded.chunk.blocks,
					chunks[index].blocks,
					sizeof(chunks[index].blocks)) != 0)
			{
				std::cerr << "Decoded ZSTD section payload mismatch for chunk "
						  << index << std::endl;
				return 1;
			}
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult decodeZstdSections;
		decodeZstdSections.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		decodeZstdSections.perChunkMs =
			decodeZstdSections.totalMs / static_cast<double>(chunkCount);
		printTimer("decode_zstd_sections_lvl3", decodeZstdSections);
	}

	std::filesystem::path databasePath =
		std::filesystem::temp_directory_path() / "voxplace_world_storage_bench.sqlite3";
	std::filesystem::path databaseBatchPath =
		std::filesystem::temp_directory_path() / "voxplace_world_storage_bench_batch.sqlite3";
	std::filesystem::path worldFilePath =
		std::filesystem::temp_directory_path() / "voxplace_world_storage_bench.world";
	std::error_code removeError;
	std::filesystem::remove(databasePath, removeError);
	std::filesystem::remove(databaseBatchPath, removeError);
	std::filesystem::remove(std::filesystem::path(databasePath.string() + "-wal"), removeError);
	std::filesystem::remove(std::filesystem::path(databasePath.string() + "-shm"), removeError);
	std::filesystem::remove(std::filesystem::path(databaseBatchPath.string() + "-wal"), removeError);
	std::filesystem::remove(std::filesystem::path(databaseBatchPath.string() + "-shm"), removeError);
	std::filesystem::remove(worldFilePath, removeError);

	{
		WorldTable table;
		if (!table.open(databasePath.string(), "bench"))
		{
			std::cerr << "Failed to open bench world DB: "
					  << table.lastError() << std::endl;
			return 1;
		}

		auto start = std::chrono::steady_clock::now();
		for (const VoxelChunkData &chunk : chunks)
		{
			if (!table.saveChunk(chunk))
			{
				std::cerr << "Failed to save bench chunk: "
						  << table.lastError() << std::endl;
				return 1;
			}
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult saveSqlite;
		saveSqlite.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		saveSqlite.perChunkMs =
			saveSqlite.totalMs / static_cast<double>(chunkCount);
		printTimer("sqlite_save_zstd_sections", saveSqlite);

		auto loadStart = std::chrono::steady_clock::now();
		for (const auto &[cx, cz] : coords)
		{
			VoxelChunkData chunk;
			if (!table.loadChunk(cx, cz, chunk))
			{
				std::cerr << "Failed to load bench chunk: "
						  << table.lastError() << std::endl;
				return 1;
			}
		}
		auto loadStop = std::chrono::steady_clock::now();
			TimerResult loadSqlite;
			loadSqlite.totalMs =
				std::chrono::duration<double, std::milli>(loadStop - loadStart).count();
			loadSqlite.perChunkMs =
				loadSqlite.totalMs / static_cast<double>(chunkCount);
			printTimer("sqlite_load_zstd_sections", loadSqlite);

			auto flyForwardStart = std::chrono::steady_clock::now();
			for (int64_t key : flyThroughKeys)
			{
				VoxelChunkData chunk;
				const auto &coord = coordByKey[key];
				if (!table.loadChunk(coord.first, coord.second, chunk))
				{
					std::cerr << "Failed to fly-through load chunk from SQLite: "
							  << table.lastError() << std::endl;
					return 1;
				}
			}
			auto flyForwardStop = std::chrono::steady_clock::now();
			TimerResult flySqliteForward;
			flySqliteForward.totalMs =
				std::chrono::duration<double, std::milli>(
					flyForwardStop - flyForwardStart)
					.count();
			flySqliteForward.perChunkMs =
				flySqliteForward.totalMs / static_cast<double>(flyThroughKeys.size());
			printTimer("sqlite_flythrough_forward_zstd_sections", flySqliteForward);

			auto flyReverseStart = std::chrono::steady_clock::now();
			for (int64_t key : reverseFlyThroughKeys)
			{
				VoxelChunkData chunk;
				const auto &coord = coordByKey[key];
				if (!table.loadChunk(coord.first, coord.second, chunk))
				{
					std::cerr << "Failed to reverse fly-through load chunk from SQLite: "
							  << table.lastError() << std::endl;
					return 1;
				}
			}
			auto flyReverseStop = std::chrono::steady_clock::now();
			TimerResult flySqliteReverse;
			flySqliteReverse.totalMs =
				std::chrono::duration<double, std::milli>(
					flyReverseStop - flyReverseStart)
					.count();
			flySqliteReverse.perChunkMs =
				flySqliteReverse.totalMs / static_cast<double>(reverseFlyThroughKeys.size());
			printTimer("sqlite_flythrough_reverse_zstd_sections", flySqliteReverse);
		}

	size_t sqliteMainBytes = fileSizeOrZero(databasePath);
	size_t sqliteWalBytes =
		fileSizeOrZero(std::filesystem::path(databasePath.string() + "-wal"));
	size_t sqliteShmBytes =
		fileSizeOrZero(std::filesystem::path(databasePath.string() + "-shm"));
	std::cout << "sqlite_world_file_bytes="
			  << (sqliteMainBytes + sqliteWalBytes + sqliteShmBytes)
			  << std::endl;

	{
		auto start = std::chrono::steady_clock::now();
		if (!writeSqliteBatch(databaseBatchPath, chunks, zstdSectionPayloads))
		{
			std::cerr << "Failed to write bench chunks in SQLite batch transaction"
					  << std::endl;
			return 1;
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult saveSqliteBatch;
		saveSqliteBatch.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		saveSqliteBatch.perChunkMs =
			saveSqliteBatch.totalMs / static_cast<double>(chunkCount);
		printTimer("sqlite_save_zstd_sections_batch_txn", saveSqliteBatch);
	}

	size_t sqliteBatchMainBytes = fileSizeOrZero(databaseBatchPath);
	size_t sqliteBatchWalBytes =
		fileSizeOrZero(std::filesystem::path(databaseBatchPath.string() + "-wal"));
	size_t sqliteBatchShmBytes =
		fileSizeOrZero(std::filesystem::path(databaseBatchPath.string() + "-shm"));
	std::cout << "sqlite_batch_world_file_bytes="
			  << (sqliteBatchMainBytes + sqliteBatchWalBytes + sqliteBatchShmBytes)
			  << std::endl;

	{
		auto start = std::chrono::steady_clock::now();
		if (!writeIndexedWorldFile(worldFilePath, chunkKeys, zstdSectionPayloads))
		{
			std::cerr << "Failed to write indexed world file" << std::endl;
			return 1;
		}
		auto stop = std::chrono::steady_clock::now();
		TimerResult writeFile;
		writeFile.totalMs =
			std::chrono::duration<double, std::milli>(stop - start).count();
		writeFile.perChunkMs =
			writeFile.totalMs / static_cast<double>(chunkCount);
		printTimer("file_write_zstd_sections", writeFile);

		std::ifstream input;
		std::unordered_map<int64_t, FileChunkIndexEntry> index;
		if (!readIndexedWorldFile(worldFilePath, input, index))
		{
			std::cerr << "Failed to read indexed world file header" << std::endl;
			return 1;
		}

		auto loadStart = std::chrono::steady_clock::now();
		for (int64_t key : chunkKeys)
		{
			VoxelChunkData chunk;
			if (!loadChunkFromIndexedWorldFile(input, index, key, chunk))
			{
				std::cerr << "Failed to sequentially load chunk from world file" << std::endl;
				return 1;
			}
		}
		auto loadStop = std::chrono::steady_clock::now();
		TimerResult readFileSequential;
		readFileSequential.totalMs =
			std::chrono::duration<double, std::milli>(loadStop - loadStart).count();
		readFileSequential.perChunkMs =
			readFileSequential.totalMs / static_cast<double>(chunkCount);
		printTimer("file_load_zstd_sections_sequential", readFileSequential);

		std::vector<int64_t> shuffledKeys = chunkKeys;
		std::minstd_rand rng(42);
		std::shuffle(shuffledKeys.begin(), shuffledKeys.end(), rng);

		auto randomLoadStart = std::chrono::steady_clock::now();
		for (int64_t key : shuffledKeys)
		{
			VoxelChunkData chunk;
			if (!loadChunkFromIndexedWorldFile(input, index, key, chunk))
			{
				std::cerr << "Failed to randomly load chunk from world file" << std::endl;
				return 1;
			}
		}
		auto randomLoadStop = std::chrono::steady_clock::now();
			TimerResult readFileRandom;
			readFileRandom.totalMs =
				std::chrono::duration<double, std::milli>(randomLoadStop - randomLoadStart).count();
			readFileRandom.perChunkMs =
				readFileRandom.totalMs / static_cast<double>(chunkCount);
			printTimer("file_load_zstd_sections_random", readFileRandom);

			auto flyForwardStart = std::chrono::steady_clock::now();
			for (int64_t key : flyThroughKeys)
			{
				VoxelChunkData chunk;
				if (!loadChunkFromIndexedWorldFile(input, index, key, chunk))
				{
					std::cerr << "Failed to forward fly-through load chunk from world file" << std::endl;
					return 1;
				}
			}
			auto flyForwardStop = std::chrono::steady_clock::now();
			TimerResult flyFileForward;
			flyFileForward.totalMs =
				std::chrono::duration<double, std::milli>(
					flyForwardStop - flyForwardStart)
					.count();
			flyFileForward.perChunkMs =
				flyFileForward.totalMs / static_cast<double>(flyThroughKeys.size());
			printTimer("file_flythrough_forward_zstd_sections", flyFileForward);

			auto flyReverseStart = std::chrono::steady_clock::now();
			for (int64_t key : reverseFlyThroughKeys)
			{
				VoxelChunkData chunk;
				if (!loadChunkFromIndexedWorldFile(input, index, key, chunk))
				{
					std::cerr << "Failed to reverse fly-through load chunk from world file" << std::endl;
					return 1;
				}
			}
			auto flyReverseStop = std::chrono::steady_clock::now();
			TimerResult flyFileReverse;
			flyFileReverse.totalMs =
				std::chrono::duration<double, std::milli>(
					flyReverseStop - flyReverseStart)
					.count();
			flyFileReverse.perChunkMs =
				flyFileReverse.totalMs / static_cast<double>(reverseFlyThroughKeys.size());
			printTimer("file_flythrough_reverse_zstd_sections", flyFileReverse);
		}

	std::cout << "world_file_bytes=" << fileSizeOrZero(worldFilePath) << std::endl;

	std::filesystem::remove(databasePath, removeError);
	std::filesystem::remove(databaseBatchPath, removeError);
	std::filesystem::remove(std::filesystem::path(databasePath.string() + "-wal"), removeError);
	std::filesystem::remove(std::filesystem::path(databasePath.string() + "-shm"), removeError);
	std::filesystem::remove(std::filesystem::path(databaseBatchPath.string() + "-wal"), removeError);
	std::filesystem::remove(std::filesystem::path(databaseBatchPath.string() + "-shm"), removeError);
	std::filesystem::remove(worldFilePath, removeError);
	return 0;
}
