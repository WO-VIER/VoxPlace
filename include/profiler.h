#ifndef PROFILER_H
#define PROFILER_H

#include <Chunk2.h>
#include <vector>
#include <string>
#include <unordered_map>

std::string formatBytes(size_t bytes);
void printChunkProfiler(const std::unordered_map<int64_t, Chunk2*>& chunkMap);

#endif // PROFILER_H
