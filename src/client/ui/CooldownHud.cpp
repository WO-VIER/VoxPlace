#include <client/ui/CooldownHud.h>

#include <imgui.h>

#include <cstdio>
#include <limits>
#include <string>

namespace
{
	constexpr const char *COOLDOWN_FONT_PATH = "assets/login/Mojangles.ttf";
	constexpr float HUD_MARGIN = 22.0f;
	constexpr float HUD_LINE_SPACING = 6.0f;

	std::string cooldownLabel(const char *name, uint64_t remainingCooldownMs)
	{
		if (remainingCooldownMs == 0)
		{
			return std::string(name) + ": READY";
		}
		double seconds = static_cast<double>(remainingCooldownMs) / 1000.0;
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%s: %.1fs", name, seconds);
		return buffer;
	}

	void drawShadowedText(ImDrawList *drawList,
					 ImFont *font,
					 const ImVec2 &position,
					 const std::string &label)
	{
		drawList->AddText(
			font,
			font->LegacySize,
			ImVec2(position.x + 2.0f, position.y + 2.0f),
			IM_COL32(32, 32, 32, 255),
			label.c_str());
		drawList->AddText(
			font,
			font->LegacySize,
			position,
			IM_COL32(255, 255, 255, 255),
			label.c_str());
	}

	void renderCooldownLabel(ImDrawList *drawList,
						ImFont *font,
						ImGuiViewport *viewport,
						const std::string &label,
						float lineOffset)
	{
		ImVec2 textSize = font->CalcTextSizeA(
			font->LegacySize,
			std::numeric_limits<float>::max(),
			0.0f,
			label.c_str());
		ImVec2 textPos(
			viewport->Pos.x + viewport->Size.x - textSize.x - HUD_MARGIN,
			viewport->Pos.y + viewport->Size.y - textSize.y - HUD_MARGIN - lineOffset);
		drawShadowedText(
			drawList,
			font,
			textPos,
			label);
	}

	void renderVoteLabel(ImDrawList *drawList,
					ImFont *font,
					ImGuiViewport *viewport,
					const ExpansionStatusMessage &expansionStatus)
	{
		if (expansionStatus.voteActive == 0)
		{
			return;
		}
		char buffer[64];
		std::snprintf(
			buffer,
			sizeof(buffer),
			"Expand votes: %u/%u",
			expansionStatus.votesCast,
			expansionStatus.eligiblePlayers);
		std::string label = buffer;
		ImVec2 textSize = font->CalcTextSizeA(
			font->LegacySize,
			std::numeric_limits<float>::max(),
			0.0f,
			label.c_str());
		ImVec2 textPos(
			viewport->Pos.x + viewport->Size.x - textSize.x - HUD_MARGIN,
			viewport->Pos.y + HUD_MARGIN);
		drawShadowedText(
			drawList,
			font,
			textPos,
			label);
	}
}

bool CooldownHud::loadAssets()
{
	ImGuiIO &io = ImGui::GetIO();
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 1;
	fontConfig.OversampleV = 1;
	fontConfig.PixelSnapH = true;

	m_font = io.Fonts->AddFontFromFileTTF(COOLDOWN_FONT_PATH, 18.0f, &fontConfig);
	if (m_font == nullptr)
	{
		m_font = io.FontDefault;
	}
	return m_font != nullptr;
}

void CooldownHud::render(bool inGame,
					 uint64_t remainingCooldownMs,
					 uint64_t remainingBlockCooldownMs,
					 const ExpansionStatusMessage *expansionStatus)
{
	if (!inGame)
	{
		return;
	}
	if (m_font == nullptr)
	{
		return;
	}

	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImDrawList *drawList = ImGui::GetForegroundDrawList();
	std::string blockLabel = cooldownLabel("Block", remainingBlockCooldownMs);
	ImVec2 blockSize = m_font->CalcTextSizeA(
		m_font->LegacySize,
		std::numeric_limits<float>::max(),
		0.0f,
		blockLabel.c_str());
	std::string expandLabel = cooldownLabel("Expand", remainingCooldownMs);
	renderCooldownLabel(
		drawList,
		m_font,
		viewport,
		blockLabel,
		0.0f);
	renderCooldownLabel(
		drawList,
		m_font,
		viewport,
		expandLabel,
		blockSize.y + HUD_LINE_SPACING);
	if (expansionStatus != nullptr)
	{
		renderVoteLabel(drawList, m_font, viewport, *expansionStatus);
	}
}
