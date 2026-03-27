#include <client/core/InputSystem.h>

namespace
{
	GameState *gInputGameState = nullptr;
	Camera *gInputCamera = nullptr;

	void framebufferSizeCallback(GLFWwindow *, int width, int height)
	{
		glViewport(0, 0, width, height);
	}

	void mouseCallback(GLFWwindow *window, double xposIn, double yposIn)
	{
		if (gInputGameState == nullptr || gInputCamera == nullptr)
		{
			return;
		}
		if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
		{
			return;
		}

		float xpos = static_cast<float>(xposIn);
		float ypos = static_cast<float>(yposIn);
		if (gInputGameState->input.firstMouse)
		{
			gInputGameState->input.lastMouseX = xpos;
			gInputGameState->input.lastMouseY = ypos;
			gInputGameState->input.firstMouse = false;
		}

		float xoffset = xpos - gInputGameState->input.lastMouseX;
		float yoffset = gInputGameState->input.lastMouseY - ypos;
		gInputGameState->input.lastMouseX = xpos;
		gInputGameState->input.lastMouseY = ypos;
		gInputCamera->ProcessMouseMovement(xoffset, yoffset);
	}

	void mouseButtonCallback(GLFWwindow *window, int button, int action, int)
	{
		if (gInputGameState == nullptr)
		{
			return;
		}
		if (gInputGameState->appState != ClientAppState::InGame)
		{
			return;
		}

		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
		{
			if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
			{
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
				gInputGameState->input.firstMouse = true;
			}
		}
	}

	void scrollCallback(GLFWwindow *, double, double yoffset)
	{
		if (gInputCamera == nullptr)
		{
			return;
		}
		gInputCamera->ProcessMouseScroll(static_cast<float>(yoffset));
	}
}

void installClientInputCallbacks(GLFWwindow *window, GameState &gameState, Camera &camera)
{
	gInputGameState = &gameState;
	gInputCamera = &camera;

	glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
	glfwSetCursorPosCallback(window, mouseCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetScrollCallback(window, scrollCallback);
}

void processGameplayInput(GLFWwindow *window, GameState &gameState, Camera &camera, float deltaTime)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(FORWARD, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(LEFT, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(RIGHT, deltaTime);
	}
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
	{
		camera.ProcessKeyboard(UP, deltaTime);
	}

	static bool previousLeftDown = false;
	static bool previousRightDown = false;
	bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	bool rightDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	bool cursorCaptured = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

	if (leftDown && !previousLeftDown && cursorCaptured)
	{
		gameState.input.breakBlockRequested = true;
	}
	if (rightDown && !previousRightDown && cursorCaptured)
	{
		gameState.input.placeBlockRequested = true;
	}

	previousLeftDown = leftDown;
	previousRightDown = rightDown;
}

void getFramebufferSize(GLFWwindow *window, const GameState &gameState, int &outWidth, int &outHeight)
{
	outWidth = gameState.display.screenWidth;
	outHeight = gameState.display.screenHeight;

	if (window != nullptr)
	{
		glfwGetFramebufferSize(window, &outWidth, &outHeight);
	}

	if (outWidth < 1)
	{
		outWidth = 1;
	}
	if (outHeight < 1)
	{
		outHeight = 1;
	}
}

float framebufferAspectRatio(GLFWwindow *window, const GameState &gameState)
{
	int framebufferWidth = 0;
	int framebufferHeight = 0;
	getFramebufferSize(window, gameState, framebufferWidth, framebufferHeight);
	return static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
}
