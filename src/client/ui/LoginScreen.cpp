#include <client/ui/LoginScreen.h>

#include <PlayerUsername.h>

#include <glad/glad.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

namespace
{
	constexpr const char *LOGIN_FONT_PATH = "assets/login/Mojangles.ttf";
	constexpr const char *LOGIN_DIRT_TEXTURE_PATH = "assets/login/Dirt_Tile.png";
	constexpr const char *LOGIN_BUTTON_NORMAL_TEXTURE_PATH = "assets/login/MainMenuButton_Norm.png";
	constexpr const char *LOGIN_BUTTON_HOVER_TEXTURE_PATH = "assets/login/MainMenuButton_Over.png";

	const std::array<const char *, 8> LOGIN_SPLASH_TEXTS = {
		"STACK YOUR PIXELS!",
		"PLACE IN 3D!",
		"VOXELS AWAIT!",
		"LOGIN CRAFTED!",
		"LOCALHOST READY!",
		"SERVER APPROVED!",
		"ZERO BLUR!",
		"SHARP AS BEDROCK!"
	};

	template <size_t N>
	void copyStringToBuffer(const std::string &value, char (&buffer)[N])
	{
		std::fill(std::begin(buffer), std::end(buffer), '\0');
		size_t count = value.size();
		if (count > N - 1)
		{
			count = N - 1;
		}
		for (size_t index = 0; index < count; index++)
		{
			buffer[index] = value[index];
		}
	}

	bool parsePortBuffer(const char *rawPort, uint16_t &port)
	{
		std::string_view rawPortView(rawPort);
		if (rawPortView.empty())
		{
			return false;
		}

		uint32_t parsed = 0;
		for (char character : rawPortView)
		{
			if (character < '0' || character > '9')
			{
				return false;
			}
			parsed *= 10;
			parsed += static_cast<uint32_t>(character - '0');
			if (parsed > 65535)
			{
				return false;
			}
		}

		if (parsed == 0)
		{
			return false;
		}

		port = static_cast<uint16_t>(parsed);
		return true;
	}
}

struct LoginScreen::UiTexture
{
	GLuint texture = 0;
	int width = 0;
	int height = 0;
	bool loaded = false;

	bool load(const char *texturePath, GLint wrapMode)
	{
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
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
		stbi_image_free(data);
		loaded = true;
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
		loaded = false;
	}

	ImTextureID imguiId() const
	{
		return static_cast<ImTextureID>(texture);
	}
};

LoginScreen::LoginScreen()
{
	std::fill(std::begin(m_host), std::end(m_host), '\0');
	std::fill(std::begin(m_port), std::end(m_port), '\0');
	std::fill(std::begin(m_username), std::end(m_username), '\0');
	std::fill(std::begin(m_password), std::end(m_password), '\0');

	m_dirtTexture = new UiTexture();
	m_buttonNormalTexture = new UiTexture();
	m_buttonHoverTexture = new UiTexture();
}

LoginScreen::~LoginScreen()
{
	cleanup();
	delete m_dirtTexture;
	delete m_buttonNormalTexture;
	delete m_buttonHoverTexture;
	m_dirtTexture = nullptr;
	m_buttonNormalTexture = nullptr;
	m_buttonHoverTexture = nullptr;
}

void LoginScreen::initialize(const LoginLaunchData &launchData)
{
	copyStringToBuffer(launchData.host, m_host);
	copyStringToBuffer(std::to_string(launchData.port), m_port);
	copyStringToBuffer(launchData.username, m_username);
	std::fill(std::begin(m_password), std::end(m_password), '\0');
	m_errorMessage.clear();
	m_statusMessage.clear();
	m_autoConnectPending = launchData.autoConnect;
}

bool LoginScreen::loadAssets()
{
	if (m_dirtTexture != nullptr)
	{
		m_dirtTexture->load(LOGIN_DIRT_TEXTURE_PATH, GL_REPEAT);
	}
	if (m_buttonNormalTexture != nullptr)
	{
		m_buttonNormalTexture->load(LOGIN_BUTTON_NORMAL_TEXTURE_PATH, GL_CLAMP_TO_EDGE);
	}
	if (m_buttonHoverTexture != nullptr)
	{
		m_buttonHoverTexture->load(LOGIN_BUTTON_HOVER_TEXTURE_PATH, GL_CLAMP_TO_EDGE);
	}

	ImGuiIO &io = ImGui::GetIO();
	ImFont *defaultFont = io.Fonts->AddFontDefault();
	io.FontDefault = defaultFont;

	ImFontConfig titleConfig;
	titleConfig.OversampleH = 1;
	titleConfig.OversampleV = 1;
	titleConfig.PixelSnapH = true;

	ImFontConfig labelConfig = titleConfig;
	ImFontConfig buttonConfig = titleConfig;

	m_titleFont = io.Fonts->AddFontFromFileTTF(LOGIN_FONT_PATH, 50.0f, &titleConfig);
	m_labelFont = io.Fonts->AddFontFromFileTTF(LOGIN_FONT_PATH, 14.0f, &labelConfig);
	m_buttonFont = io.Fonts->AddFontFromFileTTF(LOGIN_FONT_PATH, 18.0f, &buttonConfig);

	if (m_titleFont == nullptr)
	{
		m_titleFont = io.FontDefault;
	}
	if (m_labelFont == nullptr)
	{
		m_labelFont = io.FontDefault;
	}
	if (m_buttonFont == nullptr)
	{
		m_buttonFont = io.FontDefault;
	}

	return true;
}

void LoginScreen::cleanup()
{
	if (m_connectTaskActive)
	{
		m_connectFuture.wait();
		m_connectTaskActive = false;
	}
	if (m_dirtTexture != nullptr)
	{
		m_dirtTexture->cleanup();
	}
	if (m_buttonNormalTexture != nullptr)
	{
		m_buttonNormalTexture->cleanup();
	}
	if (m_buttonHoverTexture != nullptr)
	{
		m_buttonHoverTexture->cleanup();
	}
}

bool LoginScreen::maybeStartAutoConnect(WorldClient &worldClient,
										const std::function<void()> &clearWorldState,
										std::string &serverHost,
										uint16_t &serverPort,
										std::string &playerUsername)
{
	if (!m_autoConnectPending)
	{
		return false;
	}

	m_autoConnectPending = false;
	return beginConnect(worldClient, clearWorldState, serverHost, serverPort, playerUsername);
}

bool LoginScreen::renderAndMaybeStartConnect(WorldClient &worldClient,
											 const std::function<void()> &clearWorldState,
											 std::string &serverHost,
											 uint16_t &serverPort,
											 std::string &playerUsername)
{
	m_connectRequestedThisFrame = false;
	renderScreen();

	if (!isConnecting())
	{
		ImGuiIO &io = ImGui::GetIO();
		if (m_connectRequestedThisFrame ||
			(io.KeyMods == 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
		{
			beginConnect(worldClient, clearWorldState, serverHost, serverPort, playerUsername);
		}
	}

	return false;
}

LoginScreenPollResult LoginScreen::pollConnection(WorldClient &worldClient,
												  GLFWwindow *window,
												  Camera &camera)
{
	if (!m_connectTaskActive)
	{
		return LoginScreenPollResult::None;
	}

	if (m_connectFuture.wait_for(std::chrono::milliseconds(0)) !=
		std::future_status::ready)
	{
		return LoginScreenPollResult::None;
	}

	bool connected = m_connectFuture.get();
	m_connectTaskActive = false;

	if (!connected)
	{
		m_statusMessage.clear();
		m_errorMessage = worldClient.lastConnectionError();
		return LoginScreenPollResult::Failed;
	}

	camera.Position = worldClient.localPlayer().hot.position;
	m_statusMessage.clear();
	m_errorMessage.clear();
	if (window != nullptr)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	return LoginScreenPollResult::Connected;
}

void LoginScreen::handleDisconnected(const std::function<void()> &clearWorldState,
									 GLFWwindow *window,
									 const std::string &message)
{
	clearWorldState();
	m_statusMessage.clear();
	m_errorMessage = message;
	if (window != nullptr)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

bool LoginScreen::isConnecting() const
{
	return m_connectTaskActive;
}

bool LoginScreen::hasActiveTask() const
{
	return m_connectTaskActive;
}

void LoginScreen::waitForTask()
{
	if (m_connectTaskActive)
	{
		m_connectFuture.wait();
		m_connectTaskActive = false;
	}
}

bool LoginScreen::beginConnect(WorldClient &worldClient,
							   const std::function<void()> &clearWorldState,
							   std::string &serverHost,
							   uint16_t &serverPort,
							   std::string &playerUsername)
{
	if (m_connectTaskActive)
	{
		return false;
	}

	std::string host = trimPlayerUsername(m_host);
	if (host.empty())
	{
		m_errorMessage = "Server host must not be empty";
		return false;
	}

	uint16_t port = 0;
	if (!parsePortBuffer(m_port, port))
	{
		m_errorMessage = "Invalid server port";
		return false;
	}

	std::string username = trimPlayerUsername(m_username);
	PlayerUsernameValidationError usernameError = validatePlayerUsername(username);
	if (usernameError != PlayerUsernameValidationError::None)
	{
		m_errorMessage = playerUsernameValidationErrorText(usernameError);
		return false;
	}
	std::string password = std::string(m_password);
	if (password.size() > PLAYER_PASSWORD_MAX_LENGTH)
	{
		m_errorMessage = "Password is too long";
		return false;
	}

	worldClient.disconnect();
	clearWorldState();

	m_errorMessage.clear();
	m_statusMessage = "Connecting...";
	serverHost = host;
	serverPort = port;
	playerUsername = username;

	m_connectFuture = std::async(
		std::launch::async,
		[&worldClient, host, port, username, password]()
		{
			return worldClient.connectToServer(host, port, username, password);
		});
	m_connectTaskActive = true;
	return true;
}

bool LoginScreen::drawMinecraftMenuButton(const char *id,
										  const char *label,
										  float width,
										  float height,
										  bool enabled)
{
	ImDrawList *drawList = ImGui::GetWindowDrawList();
	ImVec2 cursor = ImGui::GetCursorScreenPos();

	ImGui::PushID(id);
	if (!enabled)
	{
		ImGui::BeginDisabled();
	}

	ImGui::InvisibleButton("##button", ImVec2(width, height));
	bool hovered = ImGui::IsItemHovered();
	bool clicked = enabled && ImGui::IsItemClicked(ImGuiMouseButton_Left);

	if (!enabled)
	{
		ImGui::EndDisabled();
	}

	const UiTexture *textureToUse = m_buttonNormalTexture;
	if (hovered && m_buttonHoverTexture != nullptr && m_buttonHoverTexture->loaded)
	{
		textureToUse = m_buttonHoverTexture;
	}

	if (textureToUse != nullptr && textureToUse->loaded)
	{
		drawList->AddImage(
			textureToUse->imguiId(),
			cursor,
			ImVec2(cursor.x + width, cursor.y + height));
	}
	else
	{
		ImU32 fallbackColor = hovered ? IM_COL32(170, 170, 170, 255) : IM_COL32(145, 145, 145, 255);
		drawList->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), fallbackColor);
		drawList->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), IM_COL32(0, 0, 0, 255), 0.0f, 0, 2.0f);
	}

	ImU32 textColor = enabled ? IM_COL32(255, 255, 255, 255) : IM_COL32(175, 175, 175, 255);

	ImVec2 textSize = m_buttonFont->CalcTextSizeA(
		m_buttonFont->LegacySize,
		std::numeric_limits<float>::max(),
		0.0f,
		label);
	ImVec2 textPos(
		cursor.x + (width - textSize.x) * 0.5f,
		cursor.y + (height - textSize.y) * 0.5f - 1.0f);

	drawList->AddText(
		m_buttonFont,
		m_buttonFont->LegacySize,
		ImVec2(textPos.x + 2.0f, textPos.y + 2.0f),
		IM_COL32(63, 63, 63, 255),
		label);
	drawList->AddText(
		m_buttonFont,
		m_buttonFont->LegacySize,
		textPos,
		textColor,
		label);

	ImGui::PopID();
	return clicked;
}

void LoginScreen::renderScreen()
{
	ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImDrawList *background = ImGui::GetBackgroundDrawList();
	ImVec2 screenMin = viewport->Pos;
	ImVec2 screenMax(
		viewport->Pos.x + viewport->Size.x,
		viewport->Pos.y + viewport->Size.y);

	if (m_dirtTexture != nullptr && m_dirtTexture->loaded)
	{
		float repeatX = viewport->Size.x / 160.0f;
		float repeatY = viewport->Size.y / 160.0f;
		background->AddImage(
			m_dirtTexture->imguiId(),
			screenMin,
			screenMax,
			ImVec2(0.0f, 0.0f),
			ImVec2(repeatX, repeatY));
	}

	background->AddRectFilled(screenMin, screenMax, IM_COL32(18, 12, 8, 96));

	const char *titleText = "VoxPlace";
	ImVec2 titleSize = m_titleFont->CalcTextSizeA(
		m_titleFont->LegacySize,
		std::numeric_limits<float>::max(),
		0.0f,
		titleText);
	ImVec2 titlePos(
		viewport->Pos.x + (viewport->Size.x - titleSize.x) * 0.5f,
		viewport->Pos.y + 86.0f);

	background->AddText(
		m_titleFont,
		m_titleFont->LegacySize,
		ImVec2(titlePos.x + 5.0f, titlePos.y + 5.0f),
		IM_COL32(55, 55, 55, 255),
		titleText);
	background->AddText(
		m_titleFont,
		m_titleFont->LegacySize,
		titlePos,
		IM_COL32(212, 212, 212, 255),
		titleText);

	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::Begin(
		"##login_screen",
		nullptr,
		ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBackground);

	float panelWidth = 560.0f;
	float contentX = (viewport->Size.x - panelWidth) * 0.5f;
	float topY = 250.0f;
	float fieldGap = 56.0f;

	ImGui::PushFont(m_labelFont);
	ImGui::PushItemWidth(panelWidth);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.03f, 0.03f, 0.03f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);

	ImGui::SetCursorPos(ImVec2(contentX, topY));
	ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "SERVER HOST");
	ImGui::SetCursorPos(ImVec2(contentX, topY + 22.0f));
	if (ImGui::InputText("##login_host", m_host, sizeof(m_host)))
	{
		m_errorMessage.clear();
	}

	ImGui::SetCursorPos(ImVec2(contentX, topY + fieldGap));
	ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "PORT");
	ImGui::SetCursorPos(ImVec2(contentX, topY + fieldGap + 22.0f));
	if (ImGui::InputText("##login_port", m_port, sizeof(m_port)))
	{
		m_errorMessage.clear();
	}

	ImGui::SetCursorPos(ImVec2(contentX, topY + fieldGap * 2.0f));
	ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "USERNAME");
	ImGui::SetCursorPos(ImVec2(contentX, topY + fieldGap * 2.0f + 22.0f));
	if (ImGui::InputText(
			"##login_username",
			m_username,
			sizeof(m_username),
			ImGuiInputTextFlags_EnterReturnsTrue))
	{
		m_errorMessage.clear();
	}

	ImGui::SetCursorPos(ImVec2(contentX, topY + fieldGap * 3.0f));
	ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "PASSWORD");
	ImGui::SetCursorPos(ImVec2(contentX, topY + fieldGap * 3.0f + 22.0f));
	if (ImGui::InputText(
			"##login_password",
			m_password,
			sizeof(m_password),
			ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue))
	{
		m_errorMessage.clear();
	}

	ImGui::PopStyleVar();
	ImGui::PopStyleColor(2);
	ImGui::PopItemWidth();
	ImGui::PopFont();

	float buttonWidth = panelWidth;
	float buttonHeight = 54.0f;
	ImGui::SetCursorPos(ImVec2(contentX, topY + 252.0f));
	if (drawMinecraftMenuButton(
			"connect",
			m_connectTaskActive ? "CONNECTING..." : "CONNECT",
			buttonWidth,
			buttonHeight,
			!m_connectTaskActive))
	{
		m_connectRequestedThisFrame = true;
	}

	ImGui::SetCursorPos(ImVec2(contentX, topY + 318.0f));
	if (drawMinecraftMenuButton("quit", "QUIT GAME", buttonWidth, buttonHeight, true))
	{
		glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
	}

	ImDrawList *windowDraw = ImGui::GetWindowDrawList();
	if (!m_errorMessage.empty())
	{
		windowDraw->AddText(
			m_labelFont,
			m_labelFont->LegacySize,
			ImVec2(viewport->Pos.x + contentX, viewport->Pos.y + topY + 394.0f),
			IM_COL32(255, 120, 120, 255),
			m_errorMessage.c_str());
	}
	else if (!m_statusMessage.empty())
	{
		windowDraw->AddText(
			m_labelFont,
			m_labelFont->LegacySize,
			ImVec2(viewport->Pos.x + contentX, viewport->Pos.y + topY + 394.0f),
			IM_COL32(255, 231, 45, 255),
			m_statusMessage.c_str());
	}

	ImGui::End();
}
