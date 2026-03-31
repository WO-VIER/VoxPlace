#ifndef LOGIN_SCREEN_H
#define LOGIN_SCREEN_H

#include <client/rendering/Camera.h>
#include <PlayerUsername.h>
#include <WorldClient.h>

#include <GLFW/glfw3.h>

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <future>
#include <string>

struct LoginLaunchData
{
	std::string host;
	uint16_t port = 0;
	std::string username;
	std::string password;
	bool autoConnect = false;
};

enum class LoginScreenPollResult
{
	None = 0,
	Failed = 1,
	Connected = 2
};

class LoginScreen
{
public:
	LoginScreen();
	~LoginScreen();

	void initialize(const LoginLaunchData &launchData);
	bool loadAssets();
	void cleanup();

	bool maybeStartAutoConnect(WorldClient &worldClient,
							   const std::function<void()> &clearWorldState,
							   std::string &serverHost,
							   uint16_t &serverPort,
							   std::string &playerUsername);

	bool renderAndMaybeStartConnect(WorldClient &worldClient,
									const std::function<void()> &clearWorldState,
									std::string &serverHost,
									uint16_t &serverPort,
									std::string &playerUsername);

	LoginScreenPollResult pollConnection(WorldClient &worldClient,
										 GLFWwindow *window,
										 Camera &camera);

	void handleDisconnected(const std::function<void()> &clearWorldState,
							GLFWwindow *window,
							const std::string &message);

	bool isConnecting() const;
	bool hasActiveTask() const;
	void waitForTask();

private:
	struct UiTexture;

	char m_host[128];
	char m_port[16];
	char m_username[PLAYER_USERNAME_MAX_LENGTH + 1];
	char m_password[PLAYER_PASSWORD_MAX_LENGTH + 1];
	std::string m_errorMessage;
	std::string m_statusMessage;
	bool m_autoConnectPending = false;
	bool m_connectTaskActive = false;
	std::future<bool> m_connectFuture;
	bool m_connectRequestedThisFrame = false;
	UiTexture *m_dirtTexture = nullptr;
	UiTexture *m_buttonNormalTexture = nullptr;
	UiTexture *m_buttonHoverTexture = nullptr;
	ImFont *m_titleFont = nullptr;
	ImFont *m_labelFont = nullptr;
	ImFont *m_buttonFont = nullptr;
	std::string m_splashText;

	bool beginConnect(WorldClient &worldClient,
					  const std::function<void()> &clearWorldState,
					  std::string &serverHost,
					  uint16_t &serverPort,
					  std::string &playerUsername);

	bool drawMinecraftMenuButton(const char *id,
								 const char *label,
								 float width,
								 float height,
								 bool enabled);
	void renderScreen();
};

#endif
