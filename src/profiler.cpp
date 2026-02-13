#include <profiler.h>
#include <iostream>
#include <iomanip>
#include <sstream>

std::string formatBytes(size_t bytes)
{
	const char *units[] = {"B", "KB", "MB", "GB"};
	int u = 0;
	double size = (double)bytes;
	while (size >= 1024.0 && u < 3)
	{
		size /= 1024.0;
		u++;
	}
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(2) << size << " " << units[u];
	return oss.str();
}

void printChunkProfiler(const std::unordered_map<int64_t, Chunk2*>& chunkMap)
{
	size_t totalRAM = 0, totalVRAM = 0;
	uint64_t totalVertices = 0;

	std::vector<Chunk2::ChunkStats> allStats;
	for (const auto& [key, c] : chunkMap)
	{
		auto s = c->getStats();
		allStats.push_back(s);
		totalRAM += s.ramBytes;
		totalVRAM += s.vramBytes;
		totalVertices += s.faces * 6;
	}

	std::cout << "\n";
	std::cout << "╔═══════════════════════════════════════════════════╗" << std::endl;
	std::cout << "║            VOXPLACE CHUNK PROFILER                ║" << std::endl;
	std::cout << "╠═══════════════════════════════════════════════════╣" << std::endl;

	// En-tête
	std::cout << "║ " << std::left
			  << std::setw(12) << "Chunk"
			  << std::setw(12) << "Vertices"
			  << std::setw(12) << "RAM"
			  << std::setw(12) << "VRAM"
			  << "  ║" << std::endl;
	std::cout << "╠═══════════════════════════════════════════════════╣" << std::endl;

	// Par chunk
	for (const auto &s : allStats)
	{
		std::ostringstream pos;
		pos << "(" << s.cx << "," << s.cz << ")";

		std::cout << "║ " << std::left
				  << std::setw(12) << pos.str()
				  << std::setw(12) << (s.faces * 6)
				  << std::setw(12) << formatBytes(s.ramBytes)
				  << std::setw(12) << formatBytes(s.vramBytes)
				  << "  ║" << std::endl;
	}

	// Totaux
	std::cout << "╠═══════════════════════════════════════════════════╣" << std::endl;
	std::cout << "║  TOTAL : " << chunkMap.size() << " chunks" << std::endl;
	std::cout << "║  Vertices      : " << totalVertices << std::endl;
	std::cout << "║  RAM  totale   : " << formatBytes(totalRAM) << std::endl;
	std::cout << "║  VRAM totale   : " << formatBytes(totalVRAM) << std::endl;
	std::cout << "║  RAM + VRAM    : " << formatBytes(totalRAM + totalVRAM) << std::endl;
	std::cout << "╚═══════════════════════════════════════════════════╝" << std::endl;
	std::cout << std::endl;
}
