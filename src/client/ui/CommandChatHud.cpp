#include <client/ui/CommandChatHud.h>

#include <imgui.h>

namespace
{
	constexpr const char *CHAT_FONT_PATH = "assets/login/Mojangles.ttf";
	constexpr float CHAT_WINDOW_PADDING = 22.0f;
	constexpr float CHAT_MIN_WIDTH = 280.0f;
	constexpr float CHAT_MIN_HEIGHT = 120.0f;
	constexpr ImVec4 CHAT_FRAME_BG = ImVec4(0.03f, 0.03f, 0.03f, 0.55f);
	constexpr ImVec4 CHAT_FRAME_BORDER = ImVec4(0.65f, 0.65f, 0.65f, 0.75f);
	constexpr ImVec4 CHAT_WINDOW_BG = ImVec4(0.03f, 0.03f, 0.03f, 0.40f);
	constexpr ImVec4 CHAT_GRIP = ImVec4(0.65f, 0.65f, 0.65f, 0.35f);
	constexpr ImVec4 CHAT_GRIP_ACTIVE = ImVec4(0.80f, 0.80f, 0.80f, 0.65f);

	void renderMessages(const std::deque<std::string> &messages)
	{
		for (size_t index = 0; index < messages.size(); index++)
		{
			ImGui::TextWrapped("%s", messages[index].c_str());
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
						 const std::deque<std::string> &messages,
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
	ImGui::PushStyleColor(ImGuiCol_Border, CHAT_FRAME_BORDER);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, CHAT_FRAME_BG);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, CHAT_FRAME_BG);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, CHAT_FRAME_BG);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_ResizeGrip, CHAT_GRIP);
	ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, CHAT_GRIP_ACTIVE);
	ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, CHAT_GRIP_ACTIVE);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoNav;
	ImGui::Begin("##command_chat_hud", nullptr, flags);
	float messageRegionHeight = 0.0f;
	if (open)
	{
		messageRegionHeight = -ImGui::GetFrameHeightWithSpacing() - 6.0f;
	}
	ImGui::BeginChild("##command_chat_messages", ImVec2(0.0f, messageRegionHeight), false);
	renderMessages(messages);
	if (!messages.empty() && !open)
	{
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	bool submitted = false;
	if (open)
	{
		ImGui::Separator();
		if (focusRequested)
		{
			ImGui::SetKeyboardFocusHere();
			focusRequested = false;
		}
		ImGui::SetNextItemWidth(-1.0f);
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
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(9);
	ImGui::PopFont();
	return submitted;
}
