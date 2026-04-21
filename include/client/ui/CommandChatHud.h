#ifndef CLIENT_UI_COMMAND_CHAT_HUD_H
#define CLIENT_UI_COMMAND_CHAT_HUD_H

#include <client/gameplay/ClientWorldState.h>

#include <imgui.h>

#include <cstddef>
#include <string>

class CommandChatHud
{
public:
	CommandChatHud() = default;

	bool loadAssets();
	bool render(bool inGame,
				 bool open,
				 bool &focusRequested,
				 const std::deque<ClientChatMessage> &messages,
				 char *inputBuffer,
				 size_t inputBufferSize,
				 std::string &submittedCommand);

private:
	ImFont *m_font = nullptr;
	ImVec2 m_windowSize = ImVec2(440.0f, 220.0f);
};

#endif
