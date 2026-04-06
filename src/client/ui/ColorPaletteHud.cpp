#include <glad/glad.h>

#include <client/ui/ColorPaletteHud.h>

#include <ChunkPalette.h>

#include <imgui.h>

#include <cstdio>
#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

namespace
{
	constexpr const char *PALETTE_FONT_PATH = "assets/login/Mojangles.ttf";
	constexpr const char *LEGACY_BAR_TEXTURE_PATH = "assets/hotbar_item.png";
	constexpr const char *LEGACY_SELECTED_TEXTURE_PATH = "assets/hotbar_item_selected.png";
	constexpr int VISIBLE_PALETTE_SLOTS = 9;
	constexpr float LEGACY_BAR_WIDTH = 182.0f;
	constexpr float LEGACY_BAR_HEIGHT = 22.0f;
	constexpr float LEGACY_SLOT_PITCH = 20.0f;
	constexpr float LEGACY_SLOT_SWATCH = 16.0f;
	constexpr float LEGACY_SWATCH_INSET_X = 3.0f;
	constexpr float LEGACY_SWATCH_INSET_Y = 3.0f;
	constexpr float LEGACY_SELECTED_OFFSET_X = -1.0f;
	constexpr float LEGACY_SELECTED_WIDTH = 24.0f;
	constexpr float LEGACY_SELECTED_HEIGHT = 24.0f;
	constexpr float BAR_BOTTOM_MARGIN = 26.0f;
	constexpr float LABEL_GAP = 10.0f;
	constexpr float LEGACY_BEVEL = 1.0f;

	int visibleSlotCount()
	{
		if (static_cast<int>(PLAYER_COLOR_PALETTE_SIZE) < VISIBLE_PALETTE_SLOTS)
		{
			return static_cast<int>(PLAYER_COLOR_PALETTE_SIZE);
		}
		return VISIBLE_PALETTE_SLOTS;
	}

	int clampPaletteIndex(int selectedPaletteIndex)
	{
		if (selectedPaletteIndex < 1)
		{
			return 1;
		}
		if (selectedPaletteIndex > static_cast<int>(PLAYER_COLOR_PALETTE_SIZE))
		{
			return static_cast<int>(PLAYER_COLOR_PALETTE_SIZE);
		}
		return selectedPaletteIndex;
	}

	float legacyGuiScale(const ImGuiViewport *viewport)
	{
		float guiScale = 2.0f;
		if (viewport->Size.y >= 900.0f)
		{
			guiScale = 3.0f;
		}
		if (viewport->Size.y >= 1400.0f)
		{
			guiScale = 4.0f;
		}
		return guiScale;
	}

	int firstVisibleSlot(int selectedPaletteIndex, int slotCount)
	{
		int firstSlot = selectedPaletteIndex - 1 - slotCount / 2;
		int maxFirstSlot = static_cast<int>(PLAYER_COLOR_PALETTE_SIZE) - slotCount;
		if (firstSlot < 0)
		{
			return 0;
		}
		if (firstSlot > maxFirstSlot)
		{
			return maxFirstSlot;
		}
		return firstSlot;
	}

	ImU32 paletteColorU32(uint32_t color, int alpha)
	{
		return IM_COL32(
			VoxelChunkData::colorR(color),
			VoxelChunkData::colorG(color),
			VoxelChunkData::colorB(color),
			alpha);
	}

	void formatPaletteLabel(char (&buffer)[32], int selectedPaletteIndex)
	{
		std::snprintf(
			buffer,
			sizeof(buffer),
			"PALETTE %02d/%02d",
			selectedPaletteIndex,
			static_cast<int>(PLAYER_COLOR_PALETTE_SIZE));
	}

	void drawPaletteLabel(
		ImDrawList *drawList,
		ImFont *font,
		const ImVec2 &barMin,
		float barWidth,
		int selectedPaletteIndex)
	{
		char label[32];
		formatPaletteLabel(label, selectedPaletteIndex);
		ImVec2 labelSize = font->CalcTextSizeA(
			font->LegacySize,
			std::numeric_limits<float>::max(),
			0.0f,
			label);
		ImVec2 labelPos(
			barMin.x + (barWidth - labelSize.x) * 0.5f,
			barMin.y - labelSize.y - LABEL_GAP);
		drawList->AddText(
			font,
			font->LegacySize,
			ImVec2(labelPos.x + 2.0f, labelPos.y + 2.0f),
			IM_COL32(24, 24, 24, 220),
			label);
		drawList->AddText(
			font,
			font->LegacySize,
			labelPos,
			IM_COL32(235, 235, 235, 235),
			label);
	}

	void drawLegacySlotTrack(
		ImDrawList *drawList,
		const ImVec2 &barMin,
		float guiScale)
	{
		float slotPitch = LEGACY_SLOT_PITCH * guiScale;
		float slotInner = LEGACY_SLOT_SWATCH * guiScale;
		float insetX = LEGACY_SWATCH_INSET_X * guiScale;
		float insetY = LEGACY_SWATCH_INSET_Y * guiScale;
		float bevel = LEGACY_BEVEL * guiScale;
		for (int slot = 0; slot < VISIBLE_PALETTE_SLOTS; slot++)
		{
			ImVec2 slotMin(
				barMin.x + slotPitch * static_cast<float>(slot) + insetX,
				barMin.y + insetY);
			ImVec2 slotMax(slotMin.x + slotInner, slotMin.y + slotInner);
			drawList->AddRect(
				slotMin,
				slotMax,
				IM_COL32(18, 18, 18, 200),
				0.0f,
				0,
				bevel);
		}
	}

	void drawSelectedFrame(
		ImDrawList *drawList,
		const ImVec2 &barMin,
		int selectedSlot,
		float guiScale)
	{
		float slotPitch = LEGACY_SLOT_PITCH * guiScale;
		ImVec2 frameMin(
			barMin.x + (LEGACY_SELECTED_OFFSET_X + static_cast<float>(selectedSlot) * LEGACY_SLOT_PITCH) * guiScale,
			barMin.y);
		ImVec2 frameMax(
			frameMin.x + LEGACY_SELECTED_WIDTH * guiScale,
			frameMin.y + LEGACY_SELECTED_HEIGHT * guiScale);
		float bevel = LEGACY_BEVEL * guiScale;
		drawList->AddRectFilled(frameMin, frameMax, IM_COL32(196, 196, 196, 230));
		drawList->AddRectFilled(
			ImVec2(frameMin.x + bevel, frameMin.y + bevel),
			ImVec2(frameMax.x - bevel, frameMax.y - bevel),
			IM_COL32(246, 246, 246, 245));
		drawList->AddRect(
			frameMin,
			frameMax,
			IM_COL32(12, 12, 12, 245),
			0.0f,
			0,
			bevel);
		drawList->AddRect(
			ImVec2(frameMin.x + bevel, frameMin.y + bevel),
			ImVec2(frameMax.x - bevel, frameMax.y - bevel),
			IM_COL32(112, 112, 112, 235),
			0.0f,
			0,
			bevel);
	}

	void drawPaletteSwatch(
		ImDrawList *drawList,
		const ImVec2 &barMin,
		int visibleSlot,
		uint32_t color,
		float guiScale)
	{
		float slotPitch = LEGACY_SLOT_PITCH * guiScale;
		float swatchSize = LEGACY_SLOT_SWATCH * guiScale;
		float insetX = LEGACY_SWATCH_INSET_X * guiScale;
		float insetY = LEGACY_SWATCH_INSET_Y * guiScale;
		float outline = LEGACY_BEVEL * guiScale;
		ImVec2 swatchMin(
			barMin.x + static_cast<float>(visibleSlot) * slotPitch + insetX,
			barMin.y + insetY);
		ImVec2 swatchMax(swatchMin.x + swatchSize, swatchMin.y + swatchSize);
		drawList->AddRectFilled(swatchMin, swatchMax, paletteColorU32(color, 255));
		drawList->AddRect(
			swatchMin,
			swatchMax,
			IM_COL32(12, 12, 12, 210),
			0.0f,
			0,
			outline);
	}
}

struct ColorPaletteHud::UiTexture
{
	GLuint texture = 0;
	int width = 0;
	int height = 0;

	bool load(const char *texturePath)
	{
		cleanup();
		int channels = 0;
		stbi_set_flip_vertically_on_load(true);
		unsigned char *data = stbi_load(texturePath, &width, &height, &channels, 4);
		if (data == nullptr)
		{
			return false;
		}

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RGBA,
			width,
			height,
			0,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			data);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		stbi_image_free(data);
		return true;
	}

	void cleanup()
	{
		if (texture != 0)
		{
			glDeleteTextures(1, &texture);
			texture = 0;
		}
		width = 0;
		height = 0;
	}

	ImTextureID imguiId() const
	{
		return static_cast<ImTextureID>(texture);
	}
};

ColorPaletteHud::ColorPaletteHud()
{
	m_barTexture = new UiTexture();
	m_selectedTexture = new UiTexture();
}

ColorPaletteHud::~ColorPaletteHud()
{
	cleanup();
	delete m_barTexture;
	delete m_selectedTexture;
	m_barTexture = nullptr;
	m_selectedTexture = nullptr;
}

bool ColorPaletteHud::loadAssets()
{
	if (m_barTexture != nullptr)
	{
		m_barTexture->load(LEGACY_BAR_TEXTURE_PATH);
	}
	if (m_selectedTexture != nullptr)
	{
		m_selectedTexture->load(LEGACY_SELECTED_TEXTURE_PATH);
	}

	ImGuiIO &io = ImGui::GetIO();
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 1;
	fontConfig.OversampleV = 1;
	fontConfig.PixelSnapH = true;

	m_font = io.Fonts->AddFontFromFileTTF(PALETTE_FONT_PATH, 18.0f, &fontConfig);
	if (m_font == nullptr)
	{
		m_font = io.FontDefault;
	}
	return m_font != nullptr;
}

void ColorPaletteHud::cleanup()
{
	if (m_barTexture != nullptr)
	{
		m_barTexture->cleanup();
	}
	if (m_selectedTexture != nullptr)
	{
		m_selectedTexture->cleanup();
	}
}

void ColorPaletteHud::render(bool inGame, int selectedPaletteIndex)
{
	if (!inGame || m_font == nullptr)
	{
		return;
	}

	const int slotCount = visibleSlotCount();
	const int clampedIndex = clampPaletteIndex(selectedPaletteIndex);
	const int firstSlot = firstVisibleSlot(clampedIndex, slotCount);
	ImGuiViewport *viewport = ImGui::GetMainViewport();
	float guiScale = legacyGuiScale(viewport);
	const float barWidth = LEGACY_BAR_WIDTH * guiScale;
	const float barHeight = LEGACY_BAR_HEIGHT * guiScale;
	ImDrawList *drawList = ImGui::GetForegroundDrawList();
	ImVec2 barMin(
		viewport->Pos.x + (viewport->Size.x - barWidth) * 0.5f,
		viewport->Pos.y + viewport->Size.y - barHeight - BAR_BOTTOM_MARGIN);
	ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);

	if (m_barTexture != nullptr && m_barTexture->texture != 0)
	{
		drawList->AddImage(m_barTexture->imguiId(), barMin, barMax);
	}
	else
	{
		drawList->AddRectFilled(barMin, barMax, IM_COL32(38, 38, 38, 220));
		drawList->AddRect(barMin, barMax, IM_COL32(10, 10, 10, 230), 0.0f, 0, LEGACY_BEVEL * guiScale);
	}
	drawLegacySlotTrack(drawList, barMin, guiScale);
	drawPaletteLabel(drawList, m_font, barMin, barWidth, clampedIndex);
	if (m_selectedTexture != nullptr && m_selectedTexture->texture != 0)
	{
		ImVec2 selectedMin(
			barMin.x + (LEGACY_SELECTED_OFFSET_X + static_cast<float>(clampedIndex - 1 - firstSlot) * LEGACY_SLOT_PITCH) * guiScale,
			barMin.y - 1.0f * guiScale);
		ImVec2 selectedMax(
			selectedMin.x + LEGACY_SELECTED_WIDTH * guiScale,
			selectedMin.y + LEGACY_SELECTED_HEIGHT * guiScale);
		drawList->AddImage(m_selectedTexture->imguiId(), selectedMin, selectedMax);
	}
	else
	{
		drawSelectedFrame(drawList, barMin, clampedIndex - 1 - firstSlot, guiScale);
	}

	for (int slot = 0; slot < slotCount; ++slot)
	{
		const int paletteIndex = firstSlot + slot;
		drawPaletteSwatch(
			drawList,
			barMin,
			slot,
			playerPaletteColor(static_cast<uint8_t>(paletteIndex)),
			guiScale);
	}
}
