#ifndef CHUNK_PALETTE_H
#define CHUNK_PALETTE_H

#include <VoxelChunkData.h>
#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t PLAYER_COLOR_PALETTE_SIZE = 32;

inline uint32_t playerPaletteEntry(uint32_t packedColor)
{
	return VoxelChunkData::makeColor(
		VoxelChunkData::colorR(packedColor),
		VoxelChunkData::colorG(packedColor),
		VoxelChunkData::colorB(packedColor));
}

inline const std::array<uint32_t, PLAYER_COLOR_PALETTE_SIZE> &playerColorPalette()
{
	static const std::array<uint32_t, PLAYER_COLOR_PALETTE_SIZE> palette = {
		playerPaletteEntry(0x0019016B), playerPaletteEntry(0x003700BD),
		playerPaletteEntry(0x000045FF), playerPaletteEntry(0x0000A8FE),
		playerPaletteEntry(0x0035D4FF), playerPaletteEntry(0x00B9F8FE),
		playerPaletteEntry(0x0067A201), playerPaletteEntry(0x0076CC09),
		playerPaletteEntry(0x0057EC7E), playerPaletteEntry(0x006D7502),
		playerPaletteEntry(0x00AA9D00), playerPaletteEntry(0x00BECC00),
		playerPaletteEntry(0x00A44F24), playerPaletteEntry(0x00EA9037),
		playerPaletteEntry(0x00F3E852), playerPaletteEntry(0x00BF3948),
		playerPaletteEntry(0x00FF5B69), playerPaletteEntry(0x00FFB394),
		playerPaletteEntry(0x009F1D80), playerPaletteEntry(0x00BF49B4),
		playerPaletteEntry(0x00FDABE4), playerPaletteEntry(0x007E11DD),
		playerPaletteEntry(0x008137FE), playerPaletteEntry(0x00A999FE),
		playerPaletteEntry(0x002F466D), playerPaletteEntry(0x0026699B),
		playerPaletteEntry(0x0070B4FE), playerPaletteEntry(0x00010101),
		playerPaletteEntry(0x00525252), playerPaletteEntry(0x00908D88),
		playerPaletteEntry(0x00D8D6D5), playerPaletteEntry(0x00FFFFFF)
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
