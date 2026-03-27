#ifndef CLIENT_RENDER_RENDER_SETTINGS_H
#define CLIENT_RENDER_RENDER_SETTINGS_H

enum class TerrainRenderArchitecture
{
	ChunkSsboDirect = 0,
	BigGpuBufferIndirect = 1
};

struct RenderSettings
{
	int renderDistanceChunks = 12;
	bool limitToPlayableWorld = true;
	int classicStreamingPaddingChunks = 4;
	bool useAO = true;
	bool debugSunblockOnly = false;
	int selectedPaletteIndex = 32;
	float fogStart = 80.0f;
	float fogEnd = 200.0f;
	bool minecraftFogByRenderDistance = true;
	float minecraftFogStartPercent = 0.75f;
};

#endif
