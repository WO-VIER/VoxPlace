#include <client/ui/CooldownHud.h>

#include <imgui.h>

#include <cstdio>
#include <limits>
#include <string>

namespace
{
	constexpr const char *COOLDOWN_FONT_PATH = "assets/login/Mojangles.ttf";
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

void CooldownHud::render(bool inGame, uint64_t remainingCooldownMs)
{
	if (!inGame)
	{
		return;
	}
	if (m_font == nullptr)
	{
		return;
	}

	std::string label;
	ImU32 textColor = IM_COL32(255, 255, 255, 255);
	if (remainingCooldownMs == 0)
	{
		label = "Cooldown: READY";
		textColor = IM_COL32(180, 255, 180, 255);
	}
	else
	{
		double seconds = static_cast<double>(remainingCooldownMs) / 1000.0;
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "Cooldown: %.1fs", seconds);
		label = buffer;
		textColor = IM_COL32(255, 231, 45, 255);
	}

	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImDrawList *drawList = ImGui::GetForegroundDrawList();
	ImVec2 textSize = m_font->CalcTextSizeA(
		m_font->LegacySize,
		std::numeric_limits<float>::max(),
		0.0f,
		label.c_str());

	ImVec2 textPos(
		viewport->Pos.x + 22.0f,
		viewport->Pos.y + viewport->Size.y - textSize.y - 22.0f);

	drawList->AddText(
		m_font,
		m_font->LegacySize,
		ImVec2(textPos.x + 2.0f, textPos.y + 2.0f),
		IM_COL32(32, 32, 32, 255),
		label.c_str());
	drawList->AddText(
		m_font,
		m_font->LegacySize,
		textPos,
		textColor,
		label.c_str());
}
