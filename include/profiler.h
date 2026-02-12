#ifndef PROFILER_H
#define PROFILER_H

#include <Chunk2.h>
#include <vector>
#include <string>

std::string formatBytes(size_t bytes);
void printChunkProfiler(const std::vector<Chunk2 *> &allChunks);

#endif // PROFILER_H
