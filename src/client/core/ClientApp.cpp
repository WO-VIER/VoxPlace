// GLAD doit être inclus en premier !
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#include <emscripten.h>
#else
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#endif

#include <client/core/ClientApp.h>

#include <Chunk2.h>
#include <WorldClient.h>
#include <client/core/ClientLaunch.h>
#include <client/core/ClientProfiler.h>
#include <client/core/GameState.h>
#include <client/core/InputSystem.h>
#include <client/gameplay/BlockInteractionSystem.h>
#include <client/gameplay/CameraBoundsSystem.h>
#include <client/gameplay/ChunkStreamingSystem.h>
#include <client/gameplay/ClientWorldState.h>
#include <client/gameplay/ClientWorldSystem.h>
#include <client/gameplay/MeshBuildSystem.h>
#include <client/gameplay/PlayerSyncSystem.h>
#include <client/rendering/Camera.h>
#include <client/rendering/ClientFrameRenderer.h>
#include <client/rendering/ChunkIndirectRenderer.h>
#include <client/rendering/ClientChunkMesher.h>
#include <client/rendering/Crosshair.h>
#include <client/rendering/Shader.h>
#include <client/rendering/WorldRenderer.h>
#include <client/ui/CooldownHud.h>
#include <client/ui/DebugOverlayBuilder.h>
#include <client/ui/DebugOverlay.h>
#include <client/ui/LoginScreen.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace
{
	const glm::vec3 FOG_COLOR(0.6f, 0.7f, 0.9f);

	struct ClientRuntime
	{
		GameState gameState;
		GLFWwindow *window = nullptr;
		Camera camera = Camera(glm::vec3(0.0f, 35.0f, 0.0f));
		float deltaTime = 0.0f;
		float lastFrame = 0.0f;
		float chunkRenderCpuMs = 0.0f;

		WorldClient worldClient;
		ClientWorldState worldState;
		ClientChunkMesher chunkMesher;
		ChunkIndirectRenderer chunkIndirectRenderer;
		LoginScreen loginScreen;
		CooldownHud cooldownHud;
		ClientProfilerState profilerState;
		Crosshair crosshair;
		std::unique_ptr<Shader> chunkShader;
	};

	class ClientApp
	{
	public:
		int run(int argc, char **argv)
		{
			int exitCode = 0;
			if (!initializeLaunch(argc, argv, exitCode))
			{
				return exitCode;
			}

			if (!initializeRuntime())
			{
				shutdown();
				return 1;
			}

			int runResult = runLoop();
			shutdown();
			return runResult;
		}

	private:
		ClientRuntime m_runtime;
		ClientLaunchOptions m_launchOptions;
		ClientEnvironmentOptions m_environmentOptions;
		std::chrono::steady_clock::time_point m_benchStartTime;
		bool m_glfwInitialized = false;
		bool m_imguiInitialized = false;

		bool initializeLaunch(int argc, char **argv, int &exitCode)
		{
			if (argc == 2)
			{
				std::string_view argument = argv[1];
				if (argument == "--help" || argument == "-h")
				{
					printClientUsage(argv[0]);
					exitCode = 0;
					return false;
				}
			}

			if (!parseClientLaunchOptions(argc, argv, m_launchOptions))
			{
				exitCode = 1;
				return false;
			}

			LoginLaunchData loginLaunchData;
			loginLaunchData.host = m_launchOptions.host;
			loginLaunchData.port = m_launchOptions.port;
			loginLaunchData.username = m_launchOptions.username;
			loginLaunchData.autoConnect = m_launchOptions.autoConnect;
			m_runtime.loginScreen.initialize(loginLaunchData);

			m_runtime.gameState.connection.serverHost = m_launchOptions.host;
			m_runtime.gameState.connection.serverPort = m_launchOptions.port;
			m_runtime.gameState.connection.playerUsername = m_launchOptions.username;
			m_environmentOptions = loadClientEnvironmentOptions(m_runtime.gameState);
			return true;
		}

		bool initializeRuntime()
		{
			if (!initializeWindow())
			{
				return false;
			}

			initializeOpenGlState();
			initializeUi();
			startWorldSystems();
			logStartupConfiguration();

			resetClientProfilerWindow(m_runtime.profilerState, std::chrono::steady_clock::now());
			m_benchStartTime = std::chrono::steady_clock::now();
			return true;
		}

		bool initializeWindow()
		{
			std::cout << "INITIALISATION de GLFW" << std::endl;
			if (!glfwInit())
			{
				std::cerr << "Failed to initialize GLFW" << std::endl;
				return false;
			}
			m_glfwInitialized = true;

			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

			m_runtime.window = glfwCreateWindow(
				m_runtime.gameState.display.screenWidth,
				m_runtime.gameState.display.screenHeight,
				"VoxPlace",
				nullptr,
				nullptr);
			if (m_runtime.window == nullptr)
			{
				std::cerr << "Failed to create GLFW window" << std::endl;
				return false;
			}

			glfwMakeContextCurrent(m_runtime.window);
			installClientInputCallbacks(m_runtime.window, m_runtime.gameState, m_runtime.camera);
			glfwSetInputMode(m_runtime.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

#ifndef __EMSCRIPTEN__
			if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
			{
				std::cerr << "Failed to initialize GLAD" << std::endl;
				return false;
			}
#endif

			return true;
		}

		void initializeOpenGlState()
		{
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glEnable(GL_DEPTH_TEST);
			glClearColor(FOG_COLOR.r, FOG_COLOR.g, FOG_COLOR.b, 1.0f);

			m_runtime.crosshair.init("assets/crosshair.png");
			glfwSwapInterval(0);
		}

		void initializeUi()
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO &io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			ImGui::StyleColorsDark();

			m_runtime.loginScreen.loadAssets();
			m_runtime.cooldownHud.loadAssets();
			ImGui_ImplGlfw_InitForOpenGL(m_runtime.window, true);
			ImGui_ImplOpenGL3_Init("#version 460");
			m_imguiInitialized = true;
		}

		void startWorldSystems()
		{
			m_runtime.chunkShader = std::make_unique<Shader>("src/shader/chunk2.vs", "src/shader/chunk2.fs");
			m_runtime.chunkMesher.start(m_environmentOptions.requestedMeshWorkers);
			m_runtime.chunkIndirectRenderer.init();
		}

		void logStartupConfiguration()
		{
			std::cout << "Client mesh workers: " << m_runtime.chunkMesher.workerCount() << std::endl;
			if (m_environmentOptions.requestedMeshWorkers > 0)
			{
				std::cout << "Client mesh workers override: " << m_environmentOptions.requestedMeshWorkers << std::endl;
			}
			if (m_environmentOptions.profileWorkersEnabled)
			{
				std::cout << "Client worker profiling enabled" << std::endl;
			}
			if (m_environmentOptions.benchFlyEnabled)
			{
				m_runtime.camera.Yaw = 0.0f;
				m_runtime.camera.Pitch = 0.0f;
				m_runtime.camera.ProcessMouseMovement(0.0f, 0.0f);
				m_runtime.camera.MovementSpeed = m_environmentOptions.benchFlySpeed;
				std::cout << "Client bench fly enabled at speed " << m_environmentOptions.benchFlySpeed << std::endl;
				if (m_environmentOptions.benchDurationSeconds > 0.0f)
				{
					std::cout << "Client bench duration: " << m_environmentOptions.benchDurationSeconds << " s" << std::endl;
				}
			}
			if (!m_environmentOptions.sortVisibleChunksFrontToBack)
			{
				std::cout << "Client visible chunk sorting disabled" << std::endl;
			}
		}

		int runLoop()
		{
			while (!glfwWindowShouldClose(m_runtime.window))
			{
				updateFrameTiming();
				updateConnectionState();

				if (renderLoginFrameIfNeeded())
				{
					presentFrame();
					continue;
				}

				runInGameFrame();
				maybeStopBenchRun();
				presentFrame();
			}

			return 0;
		}

		void updateFrameTiming()
		{
			float currentFrame = static_cast<float>(glfwGetTime());
			m_runtime.deltaTime = currentFrame - m_runtime.lastFrame;
			m_runtime.lastFrame = currentFrame;
		}

		void updateConnectionState()
		{
			if (m_runtime.gameState.appState == ClientAppState::Login)
			{
				if (m_runtime.loginScreen.maybeStartAutoConnect(
						m_runtime.worldClient,
						[this]()
						{
							clearClientWorldState();
						},
						m_runtime.gameState.connection.serverHost,
						m_runtime.gameState.connection.serverPort,
						m_runtime.gameState.connection.playerUsername))
				{
					m_runtime.gameState.appState = ClientAppState::Connecting;
				}
			}

			LoginScreenPollResult pollResult = m_runtime.loginScreen.pollConnection(
				m_runtime.worldClient,
				m_runtime.window,
				m_runtime.camera);
			if (pollResult == LoginScreenPollResult::Failed)
			{
				m_runtime.gameState.appState = ClientAppState::Login;
			}
			else if (pollResult == LoginScreenPollResult::Connected)
			{
				m_runtime.gameState.appState = ClientAppState::InGame;
				resetPlayerMovementSyncState(
					m_runtime.gameState,
					m_runtime.camera,
					m_runtime.worldClient);
				std::cout << "Connected as " << m_runtime.worldClient.localPlayer().cold.username
						  << " to " << m_runtime.gameState.connection.serverHost
						  << ":" << m_runtime.gameState.connection.serverPort << std::endl;
			}
			else if (m_runtime.loginScreen.isConnecting())
			{
				m_runtime.gameState.appState = ClientAppState::Connecting;
			}
			else if (m_runtime.gameState.appState != ClientAppState::InGame)
			{
				m_runtime.gameState.appState = ClientAppState::Login;
			}
		}

		bool renderLoginFrameIfNeeded()
		{
			if (m_runtime.gameState.appState == ClientAppState::InGame)
			{
				return false;
			}

			int framebufferWidth = 0;
			int framebufferHeight = 0;
			getFramebufferSize(m_runtime.window, m_runtime.gameState, framebufferWidth, framebufferHeight);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, framebufferWidth, framebufferHeight);
			glDisable(GL_DEPTH_TEST);
			glClear(GL_COLOR_BUFFER_BIT);

			beginImGuiFrame();
			m_runtime.loginScreen.renderAndMaybeStartConnect(
				m_runtime.worldClient,
				[this]()
				{
					clearClientWorldState();
				},
				m_runtime.gameState.connection.serverHost,
				m_runtime.gameState.connection.serverPort,
				m_runtime.gameState.connection.playerUsername);
			if (m_runtime.loginScreen.isConnecting())
			{
				m_runtime.gameState.appState = ClientAppState::Connecting;
			}
			renderImGuiFrame();
			return true;
		}

		void runInGameFrame()
		{
			updateDebugOverlayToggle(m_runtime.window, true, m_runtime.gameState.debugOverlayVisible);
			processGameplayInput(
				m_runtime.window,
				m_runtime.gameState,
				m_runtime.camera,
				m_runtime.deltaTime);
			if (m_environmentOptions.benchFlyEnabled)
			{
				m_runtime.camera.ProcessKeyboard(FORWARD, m_runtime.deltaTime);
			}

			ClientWorldSystem::handleEvents(
				m_runtime.worldClient,
				m_runtime.worldState,
				m_runtime.chunkIndirectRenderer,
				m_runtime.loginScreen,
				m_runtime.window,
				m_runtime.gameState,
				m_runtime.camera);
			MeshBuildSystem::drainCompletedMeshBuilds(
				m_runtime.worldState.chunkMap,
				m_runtime.worldState.pendingMeshRevisions,
				m_runtime.chunkMesher,
				m_runtime.chunkIndirectRenderer,
				m_runtime.worldState.profileMeshedChunkCountWindow,
				m_runtime.worldState.profileMeshedSectionCountWindow);
			clampCameraToPlayableWorld(
				m_runtime.worldState.frontier,
				m_runtime.worldState.hasWorldFrontier,
				m_runtime.gameState.render.limitToPlayableWorld,
				m_runtime.camera);
			maybeSendPlayerMovementSync(
				m_runtime.gameState,
				m_runtime.camera,
				m_runtime.worldClient);

			processBlockInteractionRequests();
			renderWorldFrame();
		}

		void processBlockInteractionRequests()
		{
			if (m_runtime.gameState.input.breakBlockRequested)
			{
				BlockInteractionState interactionState;
				interactionState.hasWorldFrontier = m_runtime.worldState.hasWorldFrontier;
				interactionState.frontier = &m_runtime.worldState.frontier;
				interactionState.selectedPaletteIndex = m_runtime.gameState.render.selectedPaletteIndex;
				tryBreakBlock(
					m_runtime.camera,
					interactionState,
					[this](int wx, int wy, int wz)
					{
						return MeshBuildSystem::getBlockWorld(m_runtime.worldState.chunkMap, wx, wy, wz);
					},
					m_runtime.worldClient);
				m_runtime.gameState.input.breakBlockRequested = false;
			}

			if (m_runtime.gameState.input.placeBlockRequested)
			{
				BlockInteractionState interactionState;
				interactionState.hasWorldFrontier = m_runtime.worldState.hasWorldFrontier;
				interactionState.frontier = &m_runtime.worldState.frontier;
				interactionState.selectedPaletteIndex = m_runtime.gameState.render.selectedPaletteIndex;
				tryPlaceBlock(
					m_runtime.camera,
					interactionState,
					[this](int wx, int wy, int wz)
					{
						return MeshBuildSystem::getBlockWorld(m_runtime.worldState.chunkMap, wx, wy, wz);
					},
					m_runtime.worldClient);
				m_runtime.gameState.input.placeBlockRequested = false;
			}
		}

		void renderWorldFrame()
		{
			ClientFrameRendererConfig renderConfig;
			renderConfig.classicMaxInflightChunkRequests =
				m_environmentOptions.classicMaxInflightChunkRequests;
			renderConfig.classicMaxChunkRequestsPerFrame =
				m_environmentOptions.classicMaxChunkRequestsPerFrame;
			renderConfig.sortVisibleChunksFrontToBack =
				m_environmentOptions.sortVisibleChunksFrontToBack;

			ClientFrameRenderResult frameResult = ClientFrameRenderer::renderWorldFrame(
				m_runtime.window,
				m_runtime.gameState,
				m_runtime.camera,
				m_runtime.worldClient,
				m_runtime.worldState,
				*m_runtime.chunkShader,
				m_runtime.chunkMesher,
				m_runtime.chunkIndirectRenderer,
				renderConfig,
				FOG_COLOR);
			m_runtime.chunkRenderCpuMs = frameResult.chunkRenderCpuMs;

			beginImGuiFrame();

			bool compactArenaRequested = false;
			int terrainArchitectureIndex = static_cast<int>(m_runtime.gameState.terrainRenderArchitecture);
			renderDebugAndHud(
				frameResult,
				terrainArchitectureIndex,
				compactArenaRequested);

			m_runtime.gameState.terrainRenderArchitecture =
				static_cast<TerrainRenderArchitecture>(terrainArchitectureIndex);
			if (m_runtime.gameState.terrainRenderArchitecture != m_runtime.gameState.previousTerrainRenderArchitecture)
			{
				WorldRenderer::applyArchitectureSwitch(
					m_runtime.gameState.terrainRenderArchitecture,
					m_runtime.gameState.previousTerrainRenderArchitecture,
					m_runtime.chunkIndirectRenderer,
					m_runtime.worldState.chunkMap);
			}
			if (compactArenaRequested)
			{
				m_runtime.chunkIndirectRenderer.compact();
			}

			m_runtime.crosshair.render(m_runtime.window);
			renderImGuiFrame();

			updateProfiler(frameResult.visibleChunkCount);
		}

		void renderDebugAndHud(const ClientFrameRenderResult &frameResult,
							 int &terrainArchitectureIndex,
							 bool &compactArenaRequested)
		{
			DebugOverlayBuildInputs debugOverlayInputs;
			debugOverlayInputs.gameState = &m_runtime.gameState;
			debugOverlayInputs.worldClient = &m_runtime.worldClient;
			debugOverlayInputs.worldState = &m_runtime.worldState;
			debugOverlayInputs.camera = &m_runtime.camera;
			debugOverlayInputs.chunkIndirectRenderer = &m_runtime.chunkIndirectRenderer;
			debugOverlayInputs.chunkMesher = &m_runtime.chunkMesher;
			debugOverlayInputs.crosshair = &m_runtime.crosshair;
			debugOverlayInputs.frameContext = &frameResult.frameContext;
			debugOverlayInputs.deltaTime = m_runtime.deltaTime;
			debugOverlayInputs.chunkRenderCpuMs = m_runtime.chunkRenderCpuMs;
			debugOverlayInputs.visibleChunks = frameResult.visibleChunks;
			debugOverlayInputs.totalFaces = frameResult.totalFaces;
			debugOverlayInputs.terrainArchitectureIndex = &terrainArchitectureIndex;
			debugOverlayInputs.compactArenaRequested = &compactArenaRequested;

			DebugOverlayData debugOverlayData = buildDebugOverlayData(debugOverlayInputs);
			renderDebugOverlay(m_runtime.gameState.debugOverlayVisible, debugOverlayData);
			m_runtime.cooldownHud.render(
				m_runtime.gameState.appState == ClientAppState::InGame,
				m_runtime.worldClient.remainingBlockActionCooldownMs());
		}

		void updateProfiler(size_t visibleChunkCount)
		{
			if (!m_environmentOptions.profileWorkersEnabled)
			{
				return;
			}

			accumulateClientProfilerSample(
				m_runtime.profilerState,
				static_cast<double>(m_runtime.deltaTime) * 1000.0,
				static_cast<double>(m_runtime.chunkRenderCpuMs),
				m_runtime.worldState.pendingMeshRevisions.size(),
				m_runtime.chunkMesher.pendingJobCount(),
				m_runtime.chunkMesher.completedJobCount(),
				visibleChunkCount,
				m_runtime.worldState.streamedChunkKeys.size(),
				m_runtime.worldState.profileChunkRequestsWindow,
				m_runtime.worldState.profileChunkDropsWindow,
				m_runtime.worldState.profileChunkReceivesWindow);

			CameraChunkCoord cameraChunk = cameraChunkCoord(m_runtime.camera);
			flushClientProfilerWindowIfReady(
				m_runtime.profilerState,
				std::chrono::steady_clock::now(),
				m_runtime.worldState.profileMeshedChunkCountWindow,
				m_runtime.worldState.profileMeshedSectionCountWindow,
				m_runtime.chunkMesher.workerCount(),
				m_runtime.gameState.render.renderDistanceChunks,
				m_environmentOptions.sortVisibleChunksFrontToBack,
				cameraChunk.x,
				cameraChunk.z,
				m_runtime.worldState.profileChunkRequestsWindow,
				m_runtime.worldState.profileChunkDropsWindow,
				m_runtime.worldState.profileChunkReceivesWindow,
				m_runtime.worldState.profileMeshedChunkCountWindow,
				m_runtime.worldState.profileMeshedSectionCountWindow);
		}

		void maybeStopBenchRun()
		{
			if (!m_environmentOptions.benchFlyEnabled || m_environmentOptions.benchDurationSeconds <= 0.0f)
			{
				return;
			}

			auto benchNow = std::chrono::steady_clock::now();
			auto benchElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				benchNow - m_benchStartTime);
			double benchElapsedSeconds = static_cast<double>(benchElapsed.count()) / 1000.0;
			if (benchElapsedSeconds >= static_cast<double>(m_environmentOptions.benchDurationSeconds))
			{
				glfwSetWindowShouldClose(m_runtime.window, GLFW_TRUE);
			}
		}

		void presentFrame()
		{
			glfwSwapBuffers(m_runtime.window);
			glfwPollEvents();
		}

		void clearClientWorldState()
		{
			ClientWorldSystem::clear(m_runtime.worldState, m_runtime.chunkIndirectRenderer);
		}

		void beginImGuiFrame()
		{
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
		}

		void renderImGuiFrame()
		{
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		}

		void shutdown()
		{
			m_runtime.loginScreen.waitForTask();
			m_runtime.worldClient.disconnect();
			m_runtime.chunkMesher.stop();
			m_runtime.loginScreen.cleanup();
			destroyLoadedChunks();
			m_runtime.worldState.pendingMeshRevisions.clear();
			m_runtime.worldState.streamedChunkKeys.clear();
			m_runtime.chunkIndirectRenderer.cleanup();
			m_runtime.chunkShader.reset();
			m_runtime.crosshair.cleanup();

			if (m_imguiInitialized)
			{
				ImGui_ImplOpenGL3_Shutdown();
				ImGui_ImplGlfw_Shutdown();
				ImGui::DestroyContext();
				m_imguiInitialized = false;
			}

			if (m_runtime.window != nullptr)
			{
				glfwDestroyWindow(m_runtime.window);
				m_runtime.window = nullptr;
			}
			if (m_glfwInitialized)
			{
				glfwTerminate();
				m_glfwInitialized = false;
			}
		}

		void destroyLoadedChunks()
		{
			for (auto &[key, chunk] : m_runtime.worldState.chunkMap)
			{
				delete chunk;
			}
			m_runtime.worldState.chunkMap.clear();
		}
	};
}

int runClientApplication(int argc, char **argv)
{
	ClientApp app;
	return app.run(argc, argv);
}
