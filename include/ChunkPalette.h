#ifndef CHUNK_PALETTE_H
#define CHUNK_PALETTE_H

#include <VoxelChunkData.h>
#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t PLAYER_COLOR_PALETTE_SIZE = 64;

inline uint32_t playerPaletteEntryRgb(int red, int green, int blue)
{
	return VoxelChunkData::makeColor(red, green, blue);
}

inline const std::array<uint32_t, PLAYER_COLOR_PALETTE_SIZE> &playerColorPalette()
{
	static const std::array<uint32_t, PLAYER_COLOR_PALETTE_SIZE> palette = {
		playerPaletteEntryRgb(15, 15, 15), playerPaletteEntryRgb(47, 47, 47),
		playerPaletteEntryRgb(79, 79, 79), playerPaletteEntryRgb(111, 111, 111),
		playerPaletteEntryRgb(143, 143, 143), playerPaletteEntryRgb(175, 175, 175),
		playerPaletteEntryRgb(207, 207, 207), playerPaletteEntryRgb(239, 239, 239),

		playerPaletteEntryRgb(31, 0, 0), playerPaletteEntryRgb(95, 0, 0),
		playerPaletteEntryRgb(159, 0, 0), playerPaletteEntryRgb(223, 0, 0),
		playerPaletteEntryRgb(255, 31, 31), playerPaletteEntryRgb(255, 95, 95),
		playerPaletteEntryRgb(255, 159, 159), playerPaletteEntryRgb(255, 223, 223),

		playerPaletteEntryRgb(31, 15, 0), playerPaletteEntryRgb(95, 47, 0),
		playerPaletteEntryRgb(159, 79, 0), playerPaletteEntryRgb(223, 111, 0),
		playerPaletteEntryRgb(255, 143, 31), playerPaletteEntryRgb(255, 175, 95),
		playerPaletteEntryRgb(255, 207, 159), playerPaletteEntryRgb(255, 239, 223),

		playerPaletteEntryRgb(31, 31, 0), playerPaletteEntryRgb(95, 95, 0),
		playerPaletteEntryRgb(159, 159, 0), playerPaletteEntryRgb(223, 223, 0),
		playerPaletteEntryRgb(255, 255, 31), playerPaletteEntryRgb(255, 255, 95),
		playerPaletteEntryRgb(255, 255, 159), playerPaletteEntryRgb(255, 255, 223),

		playerPaletteEntryRgb(0, 31, 0), playerPaletteEntryRgb(0, 95, 0),
		playerPaletteEntryRgb(0, 159, 0), playerPaletteEntryRgb(0, 223, 0),
		playerPaletteEntryRgb(31, 255, 31), playerPaletteEntryRgb(95, 255, 95),
		playerPaletteEntryRgb(159, 255, 159), playerPaletteEntryRgb(223, 255, 223),

		playerPaletteEntryRgb(0, 31, 31), playerPaletteEntryRgb(0, 95, 95),
		playerPaletteEntryRgb(0, 159, 159), playerPaletteEntryRgb(0, 223, 223),
		playerPaletteEntryRgb(31, 255, 255), playerPaletteEntryRgb(95, 255, 255),
		playerPaletteEntryRgb(159, 255, 255), playerPaletteEntryRgb(223, 255, 255),

		playerPaletteEntryRgb(0, 0, 31), playerPaletteEntryRgb(0, 0, 95),
		playerPaletteEntryRgb(0, 0, 159), playerPaletteEntryRgb(0, 0, 223),
		playerPaletteEntryRgb(31, 31, 255), playerPaletteEntryRgb(95, 95, 255),
		playerPaletteEntryRgb(159, 159, 255), playerPaletteEntryRgb(223, 223, 255),

		playerPaletteEntryRgb(31, 0, 31), playerPaletteEntryRgb(95, 0, 95),
		playerPaletteEntryRgb(159, 0, 159), playerPaletteEntryRgb(223, 0, 223),
		playerPaletteEntryRgb(255, 31, 255), playerPaletteEntryRgb(255, 95, 255),
		playerPaletteEntryRgb(255, 159, 255), playerPaletteEntryRgb(255, 223, 255)
	};
	return palette;
}

inline uint32_t playerPaletteColor(uint8_t paletteIndex)
{
	if (paletteIndex >= PLAYER_COLOR_PALETTE_SIZE)
	{
		return playerColorPalette().back();
	}
	return playerColorPalette()[paletteIndex];
}

#endif
