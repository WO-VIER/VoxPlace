#ifndef PROFILER_H
#define PROFILER_H

#include <ClientChunk.h>
#include <vector>
#include <string>
#include <unordered_map>

std::string formatBytes(size_t bytes);
void printChunkProfiler(const std::unordered_map<int64_t, ClientChunk *> &chunkMap);

#endif // PROFILER_H
