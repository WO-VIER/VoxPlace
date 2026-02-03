#include <Shader.h>
#include <Camera.h>
#include <TextureAtlas.h>
#include <LowResRenderer.h>
#include <cmath>
#include <config.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>
#include <glm/trigonometric.hpp>
#include <iostream>
#include <print>

// If building for Emscripten prefer the compiler-provided macro __EMSCRIPTEN__
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#include <emscripten/emscripten.h>
#else
#include "errorReporting.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#endif


void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);


static const float vertices2[] = {
	0.0, 0.0, 1.0, 0.0, 0.25, // front
	0.0, 1.0, 1.0, 0.0, 0.0,
	1.0, 1.0, 1.0, 0.25, 0.0,
	1.0, 0.0, 1.0, 0.25, 0.25,

	0.0, 0.0, 0.0, 0.0, 1.0, //   back
	0.0, 1.0, 0.0, 0.0, 0.75,
	1.0, 1.0, 0.0, 0.25, 0.75,
	1.0, 0.0, 0.0, 0.25, 1.0,

	0.0, 0.0, 1.0, 0.0, 0.50, // top
	0.0, 0.0, 0.0, 0.0, 0.75,
	1.0, 0.0, 0.0, 0.25, 0.75,
	1.0, 0.0, 1.0, 0.25, 0.50,

	0.0, 1.0, 1.0, 0.0, 0.25,  // bottom
	0.0, 1.0, 0.0, 0.0, 0.50,
	1.0, 1.0, 0.0, 0.25, 0.50,
	1.0, 1.0, 1.0, 0.25, 0.25,

	0.0, 1.0, 1.0, 0.25, 0.0, // left
	0.0, 0.0, 1.0, 0.25, 0.25,
	0.0, 0.0, 0.0, 0.50, 0.25,
	0.0, 1.0, 0.0, 0.50, 0.0,

	1.0, 1.0, 1.0, 0.50, 0.0, // right
	1.0, 0.0, 1.0, 0.50, 0.25,
	1.0, 0.0, 0.0, 0.75, 0.25,
	1.0, 1.0, 0.0, 0.75, 0.0
};

// ============================================================================
// CUBE VERTICES - Utilise les UV du TextureAtlas
// Format: x, y, z, u, v (position vec3, texCoord vec2)
// 24 vertices (4 par face) pour utilisation avec indices
// ============================================================================
using namespace TextureAtlas;

// Raccourcis pour les UV des textures
constexpr UV SIDE = GRASS_SIDE_UV;
constexpr UV BOT = DIRT_UV;
constexpr UV TOP = GRASS_TOP_UV;

static const float vertices[] = {
	// FRONT face (z = +0.5) - grass_side
	-0.5f, -0.5f,  0.5f,  SIDE.u0, SIDE.v0,  // 0: bas-gauche
	 0.5f, -0.5f,  0.5f,  SIDE.u1, SIDE.v0,  // 1: bas-droite
	 0.5f,  0.5f,  0.5f,  SIDE.u1, SIDE.v1,  // 2: haut-droite
	-0.5f,  0.5f,  0.5f,  SIDE.u0, SIDE.v1,  // 3: haut-gauche

	// BACK face (z = -0.5) - grass_side
	 0.5f, -0.5f, -0.5f,  SIDE.u0, SIDE.v0,  // 4: bas-gauche
	-0.5f, -0.5f, -0.5f,  SIDE.u1, SIDE.v0,  // 5: bas-droite
	-0.5f,  0.5f, -0.5f,  SIDE.u1, SIDE.v1,  // 6: haut-droite
	 0.5f,  0.5f, -0.5f,  SIDE.u0, SIDE.v1,  // 7: haut-gauche

	// LEFT face (x = -0.5) - grass_side
	-0.5f, -0.5f, -0.5f,  SIDE.u0, SIDE.v0,  // 8: bas-gauche
	-0.5f, -0.5f,  0.5f,  SIDE.u1, SIDE.v0,  // 9: bas-droite
	-0.5f,  0.5f,  0.5f,  SIDE.u1, SIDE.v1,  // 10: haut-droite
	-0.5f,  0.5f, -0.5f,  SIDE.u0, SIDE.v1,  // 11: haut-gauche

	// RIGHT face (x = +0.5) - grass_side
	 0.5f, -0.5f,  0.5f,  SIDE.u0, SIDE.v0,  // 12: bas-gauche
	 0.5f, -0.5f, -0.5f,  SIDE.u1, SIDE.v0,  // 13: bas-droite
	 0.5f,  0.5f, -0.5f,  SIDE.u1, SIDE.v1,  // 14: haut-droite
	 0.5f,  0.5f,  0.5f,  SIDE.u0, SIDE.v1,  // 15: haut-gauche

	// BOTTOM face (y = -0.5) - dirt
	-0.5f, -0.5f, -0.5f,  BOT.u0, BOT.v0,   // 16
	 0.5f, -0.5f, -0.5f,  BOT.u1, BOT.v0,   // 17
	 0.5f, -0.5f,  0.5f,  BOT.u1, BOT.v1,   // 18
	-0.5f, -0.5f,  0.5f,  BOT.u0, BOT.v1,   // 19

	// TOP face (y = +0.5) - grass_top (sera tinté en vert via shader)
	-0.5f,  0.5f,  0.5f,  TOP.u0, TOP.v0,   // 20
	 0.5f,  0.5f,  0.5f,  TOP.u1, TOP.v0,   // 21
	 0.5f,  0.5f, -0.5f,  TOP.u1, TOP.v1,   // 22
	-0.5f,  0.5f, -0.5f,  TOP.u0, TOP.v1    // 23
};



// Indices pour le cube (2 triangles par face, CCW winding)
unsigned int indices[] = {
	// FRONT face (vertices 0-3)
	0, 1, 2,
	2, 3, 0,

	// BACK face (vertices 4-7)
	4, 5, 6,
	6, 7, 4,

	// LEFT face (vertices 8-11)
	8, 9, 10,
	10, 11, 8,

	// RIGHT face (vertices 12-15)
	12, 13, 14,
	14, 15, 12,

	// BOTTOM face (vertices 16-19)
	16, 17, 18,
	18, 19, 16,

	// TOP face (vertices 20-23)
	20, 21, 22,
	22, 23, 20
};

const unsigned int SCREEN_WIDTH = 1920;
const unsigned int SCREEN_HEIGHT = 1080;

// global window for render callback (WASM uses callback-based main loop)
static GLFWwindow *g_window = nullptr;

//camera
Camera camera(glm::vec3(0.0f,0.0f,3.0f));
float lastX = SCREEN_WIDTH / 2.0f;
float lastY = SCREEN_HEIGHT / 2.0f;
bool firstMouse = true;

//timing
float deltaTime = 0.0f; // time betweem current and last frame
float lastFrame = 0.0f;

bool initGlfw() 
{

	std::cout << "INITIALISATION de GLFW" << std::endl;
	if (!glfwInit()) {
		std::cout << "La fenêtre GLFW n'a pas pu être initialisée !" << std::endl;
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE); // Demande un contexte de débogage
	return true;
}

void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
	glViewport(0, 0, width, height); // Opengl transforme les coordonées 2D qu'il
	// calcule en coord écran
	// Ex : (-0.5,0.5) (200, 450) (-1,1)
}

void processInput(GLFWwindow *window) 
{
	if (!window)
		return;

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
	
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera.ProcessKeyboard(UP, deltaTime);
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn)
{
	float xpos;
	float ypos;
	float xoffset;
	float yoffset;

	xpos = static_cast<float>(xposIn);
	ypos = static_cast<float>(yposIn);

	if(firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	};

	xoffset = xpos - lastX;
	yoffset = lastY - ypos;

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
};

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(static_cast<float>(yoffset));
};

void render_frame() {
	if (!g_window)
		return;
	glfwPollEvents();
	glClear(GL_COLOR_BUFFER_BIT);
	glfwSwapBuffers(g_window);
}

void pipelineTransform(Shader &shader, unsigned int atlasTexture) 
{
	glm::mat4 view = 1.0f;
	glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, 0.1f, 100.0f);

	view = camera.GetViewMatrix();
	shader.setMat4("view", view);
	shader.setMat4("projection", projection);
	
	// Pour le shader PS1: passer la résolution pour le vertex jittering
	shader.setVec2("resolution", glm::vec2(SCREEN_WIDTH, SCREEN_HEIGHT));

	// Bind l'atlas une seule fois au début
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, atlasTexture);

	for(char x = -25; x < 25; x++)
	{
		for(char z = -25; z < 25; z++)
		{
			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, glm::vec3(x, -1.0f, z));
			shader.setMat4("model", model);

			// Draw le cube entier en un seul appel (36 indices)
			glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
		}
	}
}

// Crée une texture OpenGL à partir d'un fichier image
unsigned int createTexture(const char *filename)
{
	unsigned int textureID;
	int width, height, nrChannels;
	
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);
	
	// Paramètres de texture (pixel art style)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	
	stbi_set_flip_vertically_on_load(true);
	unsigned char* data = stbi_load(filename, &width, &height, &nrChannels, 0);
	
	printf("Loading texture '%s': %p (w=%d h=%d c=%d)\n", filename, (void*)data, width, height, nrChannels);
	
	if (data)
	{
		GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
		std::cout << "Failed to load texture: " << filename << std::endl;
	
	stbi_image_free(data);
	return textureID;
}


int main() 
{
	GLFWwindow *window = g_window;

	if (!initGlfw())
		return -1;

	
	std::println("C++23 is enabled.");
	window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "VoxPlace", NULL, NULL);

#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
	// store global window for the emscripten callback
	g_window = window;
	if (!g_window) {
		std::cout << "La fenêtre n'a pas pu être créée !" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(g_window);
	// On WASM we usually don't need glad; setup viewport and clear color
	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
	emscripten_set_main_loop(render_frame, 0, 1);
#else

	if (!window) 
	{
		std::cout << "La fenêtre n'a pas pu être créée !" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Glad gere les pointeures de fonctions OpenGL
	//  	Nous passons a GLAD la fonction de recupération des pointeurs de
	//  fonctions OpenGL
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) 
	{
		std::cout << "Initialisation de GLAD échouée !" << std::endl;
		glfwTerminate();
		return -1;
	}

	// Pipeline shader : compile les shaders programmes ram -> gpu

	std::cout << "GLAD initialized successfully" << std::endl;
	if (const char *renderer = (const char *)glGetString(GL_RENDERER))
		std::cout << "OpenGL Renderer: " << renderer << std::endl;
	if (const char *version = (const char *)glGetString(GL_VERSION))
		std::cout << "OpenGL Version: " << version << std::endl;

#if defined(ENABLE_GL_DEBUG)
	enableReportGlErrors();
#endif

	/*
	//1. Vertex Shader

	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1,&vertexShaderSourceNew, NULL);
	glCompileShader(vertexShader);


	//2. Fragment shader

	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSourceNew, NULL);
	glCompileShader(fragmentShader);

	//3. Link Shader

	unsigned int shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader); //bind les shaders au programm
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);

	// Use programiv to check linking for program objects
	int sucess;
	char logs[512];
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &sucess);
	if(!sucess)
	{
					glGetProgramInfoLog(shaderProgram,sizeof(logs),NULL, logs);
					std::cout << "Erreur Linking du shader program \n" << logs <<
std::endl;
	}

// Optional: call GlReportGleeror("after shader link") if implemented in
errorReporting

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	//setup vertex data
	*/

	//Configure le state de opengl
	glEnable(GL_DEPTH_TEST);
	//glEnable(GL_CULL_FACE);
	//glFrontFace(GL_CCW);


	// Shader standard (l'effet basse résolution est géré par LowResRenderer)
	Shader shader("src/shader/3.3cube.vs", "src/shader/3.3cube.fs");
	
	// Shader PS1 alternatif (vertex jittering, affine textures, dithering)
	// Shader shader("src/shader/ps1.vs", "src/shader/ps1.fs");

	unsigned int VBO, VAO, EBO; // VBO (vertex buffer object) VAO (vertex array
															// object) EBO (element buffer object)

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	glGenBuffers(1, &EBO);
	// bind the Vertex Array Object first, then bind and set vertex buffer(s), and
	// then configure vertex attributes(s).
	glBindVertexArray(VAO);

	// VBO
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	// EBO (bind as ELEMENT_ARRAY_BUFFER while VAO is bound)
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	// Optional: call GlReportGleeror("buffers setup") if implemented in
	// errorReporting

	// Vertex attributes 
	//(layout location = 0) layout (location = 0) in vec3 aPos; // (x, y, z ) (12 bytes)
	//layout (location = 1) in vec3 aColor; // (r, g, b) (12 bytes)
	//layout (location = 2) in vec2 aTexCoord; // vec 2d for 2d textures (s, t) (x, y) (8 bytes) = 32 bytes
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0); // // position vec3 ptr->(x,y,z)(x,y)
	glEnableVertexAttribArray(0);

	// Color attributes (layout location = 1)
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float))); // textcoord vec2 (x,y,z) ptr->(x,y)
	glEnableVertexAttribArray(1);

	// Texture coord attribute
	//glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(6 * sizeof(float)));
	//glEnableVertexAttribArray(2);

	// glBindBuffer(GL_ARRAY_BUFFER,  0);

	// You can unbind the VAO afterwards so other VAO calls won't accidentally
	// modify this VAO, but this rarely happens. Modifying other
	// VAOs requires a call to glBindVertexArray anyways so we generally don't
	// unbind VAOs (nor VBOs) when it's not directly necessary.
	// glBindVertexArray(0);

	// uncomment this call to draw in wireframe polygons.
	// glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// glUseProgram(shaderProgram);

	// LOAD TEXTURE ATLAS
	unsigned int atlasTexture = createTexture("assets/terrain.png");
	
	// Initialiser le rendu basse résolution (320x240 -> fullscreen)
	LowResRenderer::init(SCREEN_WIDTH, SCREEN_HEIGHT);
	
	shader.use();
	shader.setInt("texture1", 0);

	//wireframe mode
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// Render loop
	while (!glfwWindowShouldClose(window)) 
	{
		//per-frame time logic 
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;
		
		// Imput
		processInput(window);

		// ================================================================
		// RENDU BASSE RÉSOLUTION
		// ================================================================
		
		// Commencer le rendu dans le framebuffer basse résolution
		LowResRenderer::beginFrame();
		
		// Activer le shader et rendre la scène
		shader.use();
		glBindVertexArray(VAO);
		pipelineTransform(shader, atlasTexture);
		
		// Terminer et afficher le résultat upscalé
		LowResRenderer::endFrame();

		/*
		// Récupérer l'emplacement de "model" pour dessiner nos objets
		unsigned int modelLoc = glGetUniformLocation(shader.ID, "model");


		// --- Objet 1 : Rotation ---
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));
		model = glm::rotate(model, (float)glfwGetTime(), glm::vec3(0.0f,
		0.0f, 1.0f)); glUniformMatrix4fv(modelLoc, 1, GL_FALSE,
		glm::value_ptr(model));

		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		// --- Objet 2 : Pulsation ---
		model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(-0.5f, 0.0f, 0.0f));
		float scaleAmount = static_cast<float>(sin(glfwGetTime())) * 0.5f + 0.5f;
		model = glm::scale(model, glm::vec3(scaleAmount, scaleAmount, scaleAmount));
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
		*/

		// mvp 
		
		/*
		glm::mat4 model = glm::mat4(1.0f);
		glm::mat4 view = glm::mat4(1.0f);
		glm::mat4 projection = glm::mat4(1.0f);

		view 		= glm::translate(view, glm::vec3(0.0f,0.0f,-3.0f));
		projection	= glm::perspective(glm::radians(45.0f), (float) SCREEN_WIDTH / (float) SCREEN_HEIGHT, 0.1f, 100.0f);


		shader.setMat4("projection", projection);
		shader.setMat4("view",	view);
		glBindVertexArray(VAO);

		for(unsigned int i = 0; i < 10; i++)
		{
			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, cubesPositions[i]);

			float angle = 20.0f * i;
			model = glm::rotate(model, glm::radians(angle), glm::vec3(1.0f, 0.3f, 0.5f));
			shader.setMat4("model", model);

			glDrawArrays(GL_TRIANGLES, 0, 36);
		}

		*/
		//glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
		//glDrawArrays(GL_TRIANGLES, 0, 36);

		glfwSwapBuffers(window); // Swap lower to front buffer
		glfwPollEvents();
	}

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);

	glfwTerminate();
	return 0;
#endif
}
