#ifndef TERRAIN_GENERATOR_H
#define TERRAIN_GENERATOR_H

#include <FastNoiseLite.h>
#include <TerrainColorGradients.h>
#include <VoxelChunkData.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

class TerrainGenerator
{
public:
	struct ColorProfile
	{
		uint32_t surfaceColor = 0;
		int dirtDepth = 2;
		bool rockySurface = false;
		bool dirtSurface = false;
	};

	int baseHeight = 24;
	int seaLevel = 18;
	float continentAmp = 18.0f;
	float hillAmp = 10.0f;
	float mountainAmp = 24.0f;
	float detailAmp = 1.8f;
	float riverCarveDepth = 11.0f;
	float ravineCarveDepth = 16.0f;

	TerrainGenerator(int seed = 42)
	{
		continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		continent.SetSeed(seed);
		continent.SetFrequency(0.0032f);
		continent.SetFractalType(FastNoiseLite::FractalType_FBm);
		continent.SetFractalOctaves(4);
		continent.SetFractalLacunarity(2.0f);
		continent.SetFractalGain(0.5f);

		hills.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		hills.SetSeed(seed + 11);
		hills.SetFrequency(0.007f);
		hills.SetFractalType(FastNoiseLite::FractalType_FBm);
		hills.SetFractalOctaves(3);
		hills.SetFractalLacunarity(2.0f);
		hills.SetFractalGain(0.50f);

		ridges.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		ridges.SetSeed(seed + 23);
		ridges.SetFrequency(0.0042f);
		ridges.SetFractalType(FastNoiseLite::FractalType_Ridged);
		ridges.SetFractalOctaves(4);
		ridges.SetFractalLacunarity(2.0f);
		ridges.SetFractalGain(0.5f);

		detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		detail.SetSeed(seed + 1);
		detail.SetFrequency(0.024f);
		detail.SetFractalType(FastNoiseLite::FractalType_FBm);
		detail.SetFractalOctaves(2);
		detail.SetFractalLacunarity(2.0f);
		detail.SetFractalGain(0.6f);

		biome.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		biome.SetSeed(seed + 57);
		biome.SetFrequency(0.0028f);
		biome.SetFractalType(FastNoiseLite::FractalType_FBm);
		biome.SetFractalOctaves(2);
		biome.SetFractalLacunarity(2.0f);
		biome.SetFractalGain(0.6f);

		warp.SetSeed(seed + 101);
		warp.SetDomainWarpType(FastNoiseLite::DomainWarpType_OpenSimplex2);
		warp.SetFractalType(FastNoiseLite::FractalType_DomainWarpProgressive);
		warp.SetFractalOctaves(2);
		warp.SetFrequency(0.0027f);
		warp.SetDomainWarpAmp(34.0f);

		river.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		river.SetSeed(seed + 73);
		river.SetFrequency(0.0024f);
		river.SetFractalType(FastNoiseLite::FractalType_FBm);
		river.SetFractalOctaves(2);
		river.SetFractalLacunarity(2.0f);
		river.SetFractalGain(0.55f);

		ravine.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		ravine.SetSeed(seed + 149);
		ravine.SetFrequency(0.009f);
		ravine.SetFractalType(FastNoiseLite::FractalType_FBm);
		ravine.SetFractalOctaves(2);
		ravine.SetFractalLacunarity(2.0f);
		ravine.SetFractalGain(0.55f);

		ravineArea.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
		ravineArea.SetSeed(seed + 181);
		ravineArea.SetFrequency(0.0036f);
		ravineArea.SetFractalType(FastNoiseLite::FractalType_FBm);
		ravineArea.SetFractalOctaves(2);
		ravineArea.SetFractalLacunarity(2.0f);
		ravineArea.SetFractalGain(0.5f);
	}

	static float saturate(float value)
	{
		if (value < 0.0f)
		{
			return 0.0f;
		}
		if (value > 1.0f)
		{
			return 1.0f;
		}
		return value;
	}

	static float smoothstep01(float t)
	{
		t = saturate(t);
		return t * t * (3.0f - 2.0f * t);
	}

	float computeRiverCarve(
		float worldX,
		float warpedX,
		float warpedZ,
		float continent01) const
	{
		float riverPhase = river.GetNoise(warpedX * 0.55f, warpedZ * 0.55f);
		float riverWave = std::sin(worldX * 0.0105f + riverPhase * 3.5f);
		float riverLine = 1.0f - smoothstep01(std::abs(riverWave) / 0.12f);
		float lowlandMask = 1.0f - smoothstep01((continent01 - 0.58f) / 0.20f);
		return riverLine * riverLine * lowlandMask * riverCarveDepth;
	}

	float computeRavineCarve(
		float warpedX,
		float warpedZ,
		float mountainMask) const
	{
		float ravineNoise = ravine.GetNoise(warpedX, warpedZ);
		float ravineLine = 1.0f - smoothstep01(std::abs(ravineNoise) / 0.04f);

		float ravineAreaNoise = ravineArea.GetNoise(warpedX * 0.8f, warpedZ * 0.8f);
		float ravineArea01 = 0.5f * (ravineAreaNoise + 1.0f);
		float ravinePresence = smoothstep01((ravineArea01 - 0.62f) / 0.18f);

		float strength = (0.35f + mountainMask * 0.85f) * ravineCarveDepth;
		return ravineLine * ravineLine * ravinePresence * strength;
	}

	int getHeight(int worldX, int worldZ) const
	{
		float wx = static_cast<float>(worldX);
		float wz = static_cast<float>(worldZ);

		float warpedX = wx;
		float warpedZ = wz;
		warp.DomainWarp(warpedX, warpedZ);

		float continentNoise = continent.GetNoise(warpedX, warpedZ);
		float continent01 = 0.5f * (continentNoise + 1.0f);
		float mountainMask = smoothstep01((continent01 - 0.36f) / 0.24f);
		float highlandMask = smoothstep01((continent01 - 0.25f) / 0.25f);

		float hillNoise = hills.GetNoise(warpedX, warpedZ);
		float ridgeNoise = ridges.GetNoise(warpedX, warpedZ);
		float detailNoise = detail.GetNoise(wx, wz);

		float h = static_cast<float>(baseHeight);
		h += (continent01 - 0.45f) * continentAmp;
		h += hillNoise * hillAmp * (0.85f + highlandMask * 0.65f);

		float ridge01 = 0.5f * (ridgeNoise + 1.0f);
		float ridgeShape = ridge01 * ridge01 * ridge01;
		h += ridgeShape * mountainAmp * mountainMask;

		float highlandLift = smoothstep01((continent01 - 0.58f) / 0.20f);
		h += highlandLift * 8.0f;
		h += detailNoise * detailAmp;

		h -= computeRiverCarve(wx, warpedX, warpedZ, continent01);
		h -= computeRavineCarve(warpedX, warpedZ, mountainMask);

		return std::clamp(static_cast<int>(h), 1, static_cast<int>(CHUNK_SIZE_Y) - 1);
	}

	static constexpr int DIRT_COLORS[9] = {
		0x506050,
		0x605848,
		0x705040,
		0x804838,
		0x704030,
		0x603828,
		0x503020,
		0x402818,
		0x302010
	};

	static int mod8(int value)
	{
		return ((value % 8) + 8) % 8;
	}

	static uint32_t posHash(int x, int y, int z)
	{
		uint32_t ux = static_cast<uint32_t>(x);
		uint32_t uy = static_cast<uint32_t>(y);
		uint32_t uz = static_cast<uint32_t>(z);
		uint32_t h = ux * 73856093u ^ uy * 19349663u ^ uz * 83492791u;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		return h;
	}

	static int posRand8(int x, int y, int z)
	{
		return static_cast<int>(posHash(x, y, z) & 7u);
	}

	static int posRand9(int x, int y, int z)
	{
		return static_cast<int>(posHash(x, y, z) % 9u);
	}

	static int posRand5(int x, int y, int z)
	{
		return static_cast<int>(posHash(x, y, z) % 5u);
	}

	static int lerp(int a, int b, int amt)
	{
		return a + (b - a) * amt / 8;
	}

	static int clampByte(int value)
	{
		if (value < 0)
		{
			return 0;
		}
		if (value > 255)
		{
			return 255;
		}
		return value;
	}

	static uint32_t colorFromHex(uint32_t rgb)
	{
		int red = static_cast<int>((rgb >> 16) & 0xFFu);
		int green = static_cast<int>((rgb >> 8) & 0xFFu);
		int blue = static_cast<int>(rgb & 0xFFu);
		return VoxelChunkData::makeColor(red, green, blue);
	}

	static uint32_t applyMonochromeJitter(uint32_t color, int jitter)
	{
		int red = VoxelChunkData::colorR(color) + jitter;
		int green = VoxelChunkData::colorG(color) + jitter;
		int blue = VoxelChunkData::colorB(color) + jitter;
		return VoxelChunkData::makeColor(red, green, blue);
	}

	static uint32_t blendColors(uint32_t leftColor, uint32_t rightColor, float rightFactor)
	{
		rightFactor = saturate(rightFactor);
		float leftFactor = 1.0f - rightFactor;

		int red = static_cast<int>(
			static_cast<float>(VoxelChunkData::colorR(leftColor)) * leftFactor +
			static_cast<float>(VoxelChunkData::colorR(rightColor)) * rightFactor);
		int green = static_cast<int>(
			static_cast<float>(VoxelChunkData::colorG(leftColor)) * leftFactor +
			static_cast<float>(VoxelChunkData::colorG(rightColor)) * rightFactor);
		int blue = static_cast<int>(
			static_cast<float>(VoxelChunkData::colorB(leftColor)) * leftFactor +
			static_cast<float>(VoxelChunkData::colorB(rightColor)) * rightFactor);
		return VoxelChunkData::makeColor(red, green, blue);
	}

	static uint32_t blendColorsWeighted(
		uint32_t leftColor,
		int leftWeight,
		uint32_t rightColor,
		int rightWeight)
	{
		int totalWeight = leftWeight + rightWeight;
		if (totalWeight <= 0)
		{
			return leftColor;
		}

		int red = (
			VoxelChunkData::colorR(leftColor) * leftWeight +
			VoxelChunkData::colorR(rightColor) * rightWeight) / totalWeight;
		int green = (
			VoxelChunkData::colorG(leftColor) * leftWeight +
			VoxelChunkData::colorG(rightColor) * rightWeight) / totalWeight;
		int blue = (
			VoxelChunkData::colorB(leftColor) * leftWeight +
			VoxelChunkData::colorB(rightColor) * rightWeight) / totalWeight;
		return VoxelChunkData::makeColor(red, green, blue);
	}

	static uint32_t microVary(uint32_t baseHex, int x, int y, int z)
	{
		int red = static_cast<int>((baseHex >> 16) & 0xFFu);
		int green = static_cast<int>((baseHex >> 8) & 0xFFu);
		int blue = static_cast<int>(baseHex & 0xFFu);

		int rngRed = posRand8(x, y, z);
		int rngGreen = posRand8(x + 37, y, z + 59);
		int rngBlue = posRand8(x + 71, y + 13, z);

		red += rngRed - 3;
		green += rngGreen - 3;
		blue += rngBlue - 3;

		return VoxelChunkData::makeColor(red, green, blue);
	}

	static uint32_t sampleGradientColor(const std::array<uint32_t, 64> &gradient, int index)
	{
		index = std::clamp(index, 0, 63);
		return colorFromHex(gradient[index]);
	}

	static uint32_t sampleSurfaceGradient(int height, float biome01, int worldX, int worldZ)
	{
		int gradientIndex = static_cast<int>(CHUNK_SIZE_Y) - 1 - height;
		gradientIndex = std::clamp(gradientIndex, 0, 63);

		uint32_t grassSurface = sampleGradientColor(
			TerrainColorGradients::GRASS_GRADIENT_64,
			gradientIndex);
		uint32_t hillSurface = sampleGradientColor(
			TerrainColorGradients::HILL_GRADIENT_64,
			gradientIndex);
		uint32_t snowSurface = sampleGradientColor(
			TerrainColorGradients::SNOW_GRADIENT_64,
			gradientIndex);

		float lushBlend = smoothstep01((biome01 - 0.38f) / 0.24f);
		uint32_t baseSurface = blendColors(hillSurface, grassSurface, lushBlend);

		float snowBlend = smoothstep01((static_cast<float>(height) - 44.0f) / 10.0f);
		uint32_t surfaceColor = blendColors(baseSurface, snowSurface, snowBlend);

		int monochromeJitter = posRand9(worldX + 211, height + 97, worldZ + 389) - 4;
		return applyMonochromeJitter(surfaceColor, monochromeJitter);
	}

	static uint32_t dirtColor(int x, int y, int z)
	{
		int verticalSlice = (static_cast<int>(CHUNK_SIZE_Y) - 1 - y) / 8;
		int lerpAmount = (static_cast<int>(CHUNK_SIZE_Y) - 1 - y) % 8;

		if (verticalSlice < 0)
		{
			verticalSlice = 0;
		}
		if (verticalSlice >= 8)
		{
			verticalSlice = 7;
		}

		int dirtBaseColor = DIRT_COLORS[verticalSlice];
		int dirtNextColor = DIRT_COLORS[verticalSlice + 1];

		int red = lerp(dirtBaseColor & 0xFF0000, dirtNextColor & 0xFF0000, lerpAmount) >> 16;
		int green = lerp(dirtBaseColor & 0x00FF00, dirtNextColor & 0x00FF00, lerpAmount) >> 8;
		int blue = lerp(dirtBaseColor & 0x0000FF, dirtNextColor & 0x0000FF, lerpAmount);

		int rng = posRand8(x, y, z);
		red += 4 * std::abs(mod8(x) - 4) + rng;
		green += 4 * std::abs(mod8(z) - 4) + rng;
		blue += 4 * std::abs(mod8((static_cast<int>(CHUNK_SIZE_Y) - 1 - y)) - 4) + rng;

		red = clampByte(red);
		green = clampByte(green);
		blue = clampByte(blue);
		return VoxelChunkData::makeColor(red, green, blue);
	}

	uint32_t waterColor(int x, int y, int z) const
	{
		uint32_t shallowWater = VoxelChunkData::makeColor(54, 164, 164);
		uint32_t deepWater = VoxelChunkData::makeColor(34, 116, 126);
		int waterDepth = seaLevel - y;
		float deepBlend = smoothstep01(static_cast<float>(waterDepth) / 8.0f);
		uint32_t water = blendColors(shallowWater, deepWater, deepBlend);
		int jitter = posRand5(x + 19, y + 53, z + 131) - 2;
		return applyMonochromeJitter(water, jitter);
	}

	static uint32_t blendSurfaceToDirt(
		uint32_t surfaceColor,
		uint32_t dirtColorValue,
		int depthFromSurface,
		int slope)
	{
		int surfaceWeight = 4;
		if (depthFromSurface == 1)
		{
			surfaceWeight = 3;
		}
		else if (depthFromSurface == 2)
		{
			surfaceWeight = 2;
		}
		else if (depthFromSurface == 3)
		{
			surfaceWeight = 1;
		}
		else if (depthFromSurface > 3)
		{
			surfaceWeight = 0;
		}

		if (slope >= 4)
		{
			surfaceWeight = static_cast<int>(std::round(static_cast<float>(surfaceWeight) * 0.35f));
		}

		surfaceWeight = std::clamp(surfaceWeight, 0, 4);
		int dirtWeight = 4 - surfaceWeight;
		return blendColorsWeighted(surfaceColor, surfaceWeight, dirtColorValue, dirtWeight);
	}

	static uint32_t stoneColor(int x, int y, int z)
	{
		return microVary(0x5E5E64u, x, y, z);
	}

	ColorProfile buildColorProfile(
		int worldX,
		int worldZ,
		int height,
		int slope,
		float biome01) const
	{
		ColorProfile profile;
		profile.surfaceColor = sampleSurfaceGradient(height, biome01, worldX, worldZ);

		if (slope >= 8)
		{
			profile.rockySurface = true;
		}
		else if (slope >= 4)
		{
			profile.dirtSurface = true;
		}

		if (biome01 > 0.65f)
		{
			profile.dirtDepth = 4;
		}
		else if (biome01 > 0.45f)
		{
			profile.dirtDepth = 3;
		}

		if (profile.rockySurface)
		{
			profile.dirtDepth = 1;
		}
		else if (profile.dirtSurface)
		{
			profile.dirtDepth = 2;
		}

		if (height <= seaLevel + 1)
		{
			uint32_t shoreDirt = dirtColor(worldX, height, worldZ);
			profile.surfaceColor = blendColors(profile.surfaceColor, shoreDirt, 0.45f);
		}

		return profile;
	}

	void fillChunk(VoxelChunkData &chunk) const
	{
		chunk.clearBlocks();
		for (int x = 0; x < CHUNK_SIZE_X; x++)
		{
			for (int z = 0; z < CHUNK_SIZE_Z; z++)
			{
				int worldX = chunk.chunkX * CHUNK_SIZE_X + x;
				int worldZ = chunk.chunkZ * CHUNK_SIZE_Z + z;
				int height = getHeight(worldX, worldZ);
				float biome01 = 0.5f * (biome.GetNoise(
					static_cast<float>(worldX),
					static_cast<float>(worldZ)) + 1.0f);

				int eastH = getHeight(worldX + 1, worldZ);
				int westH = getHeight(worldX - 1, worldZ);
				int northH = getHeight(worldX, worldZ + 1);
				int southH = getHeight(worldX, worldZ - 1);
				int slope = std::abs(eastH - westH) + std::abs(northH - southH);

				ColorProfile profile = buildColorProfile(worldX, worldZ, height, slope, biome01);
				chunk.blocks[x][0][z] = stoneColor(worldX, 0, worldZ);

				for (int y = 1; y <= height && y < CHUNK_SIZE_Y; y++)
				{
					if (profile.rockySurface)
					{
						chunk.blocks[x][y][z] = stoneColor(worldX, y, worldZ);
					}
					else
					{
						int depthFromSurface = height - y;
						uint32_t dirtLayerColor = dirtColor(worldX, y, worldZ);
						if (depthFromSurface <= 3)
						{
							chunk.blocks[x][y][z] = blendSurfaceToDirt(
								profile.surfaceColor,
								dirtLayerColor,
								depthFromSurface,
								slope);
						}
						else if (y > height - profile.dirtDepth)
						{
							chunk.blocks[x][y][z] = dirtLayerColor;
						}
						else
						{
							chunk.blocks[x][y][z] = stoneColor(worldX, y, worldZ);
						}
					}
				}

				if (height < seaLevel)
				{
					for (int y = height + 1; y <= seaLevel && y < CHUNK_SIZE_Y; y++)
					{
						chunk.blocks[x][y][z] = waterColor(worldX, y, worldZ);
					}
				}
			}
		}

		if (chunk.revision == 0)
		{
			chunk.revision = 1;
		}
		else
		{
			chunk.revision++;
		}
	}

private:
	FastNoiseLite continent;
	FastNoiseLite hills;
	FastNoiseLite ridges;
	FastNoiseLite detail;
	FastNoiseLite biome;
	FastNoiseLite warp;
	FastNoiseLite river;
	FastNoiseLite ravine;
	FastNoiseLite ravineArea;
};

#endif
