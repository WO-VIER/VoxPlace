#include <client/ui/CommandChatHud.h>

#include <imgui.h>

#include <cfloat>

#include <chrono>

#include <algorithm>

#include <vector>

namespace
{
	constexpr const char *CHAT_FONT_PATH = "assets/login/Mojangles.ttf";
	constexpr float CHAT_WINDOW_PADDING = 22.0f;
	constexpr float CHAT_MIN_WIDTH = 280.0f;
	constexpr float CHAT_MIN_HEIGHT = 120.0f;
	constexpr float CHAT_TEXT_PADDING_X = 6.0f;
	constexpr float CHAT_TEXT_PADDING_Y = 4.0f;
	constexpr float CHAT_LINE_GAP = 4.0f;
	constexpr float CHAT_INPUT_GAP = 6.0f;
	constexpr float CHAT_PREFIX_GAP = 6.0f;
	constexpr float CHAT_INPUT_BAR_HEIGHT = 24.0f;
	constexpr ImVec4 CHAT_WINDOW_BG = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	constexpr ImVec4 CHAT_GRIP = ImVec4(0.65f, 0.65f, 0.65f, 0.35f);
	constexpr ImVec4 CHAT_GRIP_ACTIVE = ImVec4(0.80f, 0.80f, 0.80f, 0.65f);
	constexpr ImU32 CHAT_BAR_BG = IM_COL32(0, 0, 0, 128);
	constexpr ImU32 CHAT_PLAYER_TEXT = IM_COL32(224, 224, 224, 255);
	constexpr ImU32 CHAT_SYSTEM_TEXT = IM_COL32(224, 224, 224, 255);
	constexpr ImU32 CHAT_ERROR_TEXT = IM_COL32(255, 96, 96, 255);
	constexpr uint64_t CHAT_VISIBLE_DURATION_MS = 20ull * 10ull * 50ull;

	struct ChatBlock
	{
		std::string label;
		ImU32 textColor = CHAT_PLAYER_TEXT;
		float height = 0.0f;
	};

	uint64_t systemNowMs()
	{
		auto now = std::chrono::system_clock::now().time_since_epoch();
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
	}

	float minecraftChatOpacity(uint64_t ageMs)
	{
		if (ageMs >= CHAT_VISIBLE_DURATION_MS)
		{
			return 0.0f;
		}
		double t = static_cast<double>(ageMs) / static_cast<double>(CHAT_VISIBLE_DURATION_MS);
		t = 1.0 - t;
		t = t * 10.0;
		if (t < 0.0)
		{
			t = 0.0;
		}
		if (t > 1.0)
		{
			t = 1.0;
		}
		t = t * t;
		return static_cast<float>(t);
	}

	ImU32 applyAlpha(ImU32 color, float alpha)
	{
		int red = static_cast<int>(color & 0xFFu);
		int green = static_cast<int>((color >> 8) & 0xFFu);
		int blue = static_cast<int>((color >> 16) & 0xFFu);
		int sourceAlpha = static_cast<int>((color >> 24) & 0xFFu);
		int finalAlpha = static_cast<int>(static_cast<float>(sourceAlpha) * alpha);
		if (finalAlpha < 0)
		{
			finalAlpha = 0;
		}
		if (finalAlpha > 255)
		{
			finalAlpha = 255;
		}
		return IM_COL32(red, green, blue, finalAlpha);
	}

	ImU32 chatTextColor(ServerChatMessageKind kind)
	{
		if (kind == ServerChatMessageKind::Error)
		{
			return CHAT_ERROR_TEXT;
		}
		if (kind == ServerChatMessageKind::System)
		{
			return CHAT_SYSTEM_TEXT;
		}
		return CHAT_PLAYER_TEXT;
	}

	std::string chatLineLabel(const ClientChatMessage &message)
	{
		if (message.username.empty())
		{
			return message.text;
		}
		return message.username + ": " + message.text;
	}

	std::vector<ChatBlock> collectVisibleBlocks(const std::deque<ClientChatMessage> &messages,
									 float wrapWidth,
									 float availableHeight)
	{
		std::vector<ChatBlock> blocks;
		float usedHeight = 0.0f;
		for (size_t offset = 0; offset < messages.size(); offset++)
		{
			const ClientChatMessage &message = messages[messages.size() - 1 - offset];
			ChatBlock block;
			block.label = chatLineLabel(message);
			block.textColor = chatTextColor(message.kind);
			ImVec2 textSize = ImGui::CalcTextSize(block.label.c_str(), nullptr, false, wrapWidth);
			block.height = textSize.y + CHAT_TEXT_PADDING_Y * 2.0f;
			float requiredHeight = block.height;
			if (!blocks.empty())
			{
				requiredHeight += CHAT_LINE_GAP;
			}
			if (usedHeight + requiredHeight > availableHeight)
			{
				break;
			}
			usedHeight += requiredHeight;
			blocks.push_back(block);
		}
		std::reverse(blocks.begin(), blocks.end());
		return blocks;
	}

	void renderVisibleBlocks(ImDrawList *drawList,
						  ImFont *font,
						  const ImVec2 &origin,
						  float width,
						  float availableHeight,
						  float opacity,
						  const std::vector<ChatBlock> &blocks)
	{
		if (blocks.empty())
		{
			return;
		}

		float totalHeight = 0.0f;
		for (size_t index = 0; index < blocks.size(); index++)
		{
			totalHeight += blocks[index].height;
			if (index + 1 < blocks.size())
			{
				totalHeight += CHAT_LINE_GAP;
			}
		}

		ImVec2 boxMin(origin.x, origin.y + availableHeight - totalHeight);
		ImVec2 boxMax(origin.x + width, origin.y + availableHeight);
		drawList->AddRectFilled(boxMin, boxMax, applyAlpha(CHAT_BAR_BG, opacity));

		float currentY = boxMin.y;
		for (size_t index = 0; index < blocks.size(); index++)
		{
			const ChatBlock &block = blocks[index];
				drawList->AddText(
				font,
				font->LegacySize,
				ImVec2(origin.x + CHAT_TEXT_PADDING_X, currentY + CHAT_TEXT_PADDING_Y),
				applyAlpha(block.textColor, opacity),
				block.label.c_str(),
				nullptr,
				width - CHAT_TEXT_PADDING_X * 2.0f);
			currentY += block.height + CHAT_LINE_GAP;
		}
	}
}

bool CommandChatHud::loadAssets()
{
	ImGuiIO &io = ImGui::GetIO();
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 1;
	fontConfig.OversampleV = 1;
	fontConfig.PixelSnapH = true;

	m_font = io.Fonts->AddFontFromFileTTF(CHAT_FONT_PATH, 18.0f, &fontConfig);
	if (m_font == nullptr)
	{
		m_font = io.FontDefault;
	}
	return m_font != nullptr;
}

bool CommandChatHud::render(bool inGame,
						 bool open,
						 bool &focusRequested,
						 const std::deque<ClientChatMessage> &messages,
						 char *inputBuffer,
						 size_t inputBufferSize,
						 std::string &submittedCommand)
{
	if (!inGame || m_font == nullptr)
	{
		return false;
	}
	if (!open && messages.empty())
	{
		return false;
	}

	float chatOpacity = 1.0f;
	if (!open)
	{
		uint64_t latestMessageAtMs = 0;
		for (size_t index = 0; index < messages.size(); index++)
		{
			if (messages[index].receivedAtMs > latestMessageAtMs)
			{
				latestMessageAtMs = messages[index].receivedAtMs;
			}
		}
		if (latestMessageAtMs == 0)
		{
			return false;
		}
		uint64_t nowMs = systemNowMs();
		if (nowMs < latestMessageAtMs)
		{
			chatOpacity = 1.0f;
		}
		else
		{
			chatOpacity = minecraftChatOpacity(nowMs - latestMessageAtMs);
		}
		if (chatOpacity <= 0.0f)
		{
			return false;
		}
	}

	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(
		viewport->Pos.x + CHAT_WINDOW_PADDING,
		viewport->Pos.y + viewport->Size.y - CHAT_WINDOW_PADDING),
		ImGuiCond_Always,
		ImVec2(0.0f, 1.0f));
	ImGui::SetNextWindowSize(m_windowSize, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(
		ImVec2(CHAT_MIN_WIDTH, CHAT_MIN_HEIGHT),
		ImVec2(viewport->Size.x * 0.7f, viewport->Size.y * 0.7f));
	ImGui::PushFont(m_font);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, CHAT_WINDOW_BG);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ResizeGrip, CHAT_GRIP);
	ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, CHAT_GRIP_ACTIVE);
	ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, CHAT_GRIP_ACTIVE);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.88f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	ImGui::Begin("##command_chat_hud", nullptr, flags);
	ImDrawList *drawList = ImGui::GetWindowDrawList();
	ImVec2 windowPos = ImGui::GetWindowPos();
	float messageRegionHeight = m_windowSize.y;
	if (open)
	{
		messageRegionHeight -= CHAT_INPUT_BAR_HEIGHT + CHAT_INPUT_GAP;
	}
	std::vector<ChatBlock> blocks = collectVisibleBlocks(
		messages,
		m_windowSize.x - CHAT_TEXT_PADDING_X * 2.0f,
		messageRegionHeight);
	renderVisibleBlocks(
		drawList,
		m_font,
		windowPos,
		m_windowSize.x,
		messageRegionHeight,
		chatOpacity,
		blocks);

	bool submitted = false;
	if (open)
	{
		float inputY = m_windowSize.y - CHAT_INPUT_BAR_HEIGHT;
		drawList->AddRectFilled(
			ImVec2(windowPos.x, windowPos.y + inputY),
			ImVec2(windowPos.x + m_windowSize.x, windowPos.y + m_windowSize.y),
			CHAT_BAR_BG);
		ImVec2 promptPos(windowPos.x + CHAT_TEXT_PADDING_X, windowPos.y + inputY + CHAT_TEXT_PADDING_Y);
		drawList->AddText(m_font, m_font->LegacySize, promptPos, CHAT_PLAYER_TEXT, ">");
		ImVec2 promptSize = m_font->CalcTextSizeA(
			m_font->LegacySize,
			FLT_MAX,
			0.0f,
			">");
		ImGui::SetCursorPos(ImVec2(
			promptSize.x + CHAT_TEXT_PADDING_X + CHAT_PREFIX_GAP,
			inputY + 2.0f));
		if (focusRequested)
		{
			ImGui::SetKeyboardFocusHere();
			focusRequested = false;
		}
		ImGui::SetNextItemWidth(
			m_windowSize.x - promptSize.x - CHAT_TEXT_PADDING_X * 2.0f - CHAT_PREFIX_GAP);
		if (ImGui::InputText(
				"##command_input",
				inputBuffer,
				inputBufferSize,
				ImGuiInputTextFlags_EnterReturnsTrue))
		{
			submittedCommand = inputBuffer;
			inputBuffer[0] = '\0';
			submitted = true;
		}
	}
	m_windowSize = ImGui::GetWindowSize();

	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(9);
	ImGui::PopFont();
	return submitted;
}
