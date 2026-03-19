#ifndef CHUNK_PALETTE_H
#define CHUNK_PALETTE_H

#include <VoxelChunkData.h>
#include <array>
#include <cstddef>
#include <cstdint>

constexpr size_t PLAYER_COLOR_PALETTE_SIZE = 32;

inline const std::array<uint32_t, PLAYER_COLOR_PALETTE_SIZE> &playerColorPalette()
{
	static const std::array<uint32_t, PLAYER_COLOR_PALETTE_SIZE> palette = {
		0x0019016B, 0x003700BD, 0x000045FF, 0x0000A8FE,
		0x0035D4FF, 0x00B9F8FE, 0x0067A201, 0x0076CC09,
		0x0057EC7E, 0x006D7502, 0x00AA9D00, 0x00BECC00,
		0x00A44F24, 0x00EA9037, 0x00F3E852, 0x00BF3948,
		0x00FF5B69, 0x00FFB394, 0x009F1D80, 0x00BF49B4,
		0x00FDABE4, 0x007E11DD, 0x008137FE, 0x00A999FE,
		0x002F466D, 0x0026699B, 0x0070B4FE, 0x00000000,
		0x00525252, 0x00908D88, 0x00D8D6D5, 0x00FFFFFF
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
