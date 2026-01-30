#include <Shader.h>
#include <Camera.h>
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

const unsigned int SCREEN_WIDTH = 1920;
const unsigned int SCREEN_HEIGHT = 1080;

// global window for render callback (WASM uses callback-based main loop)
static GLFWwindow *g_window = nullptr;

//camera
glm::vec3 cameraPos = glm::vec3(0.0f,0.0f,3.0f);
glm::vec3 cameraFront = glm::vec3(0.0f,0.0f,-1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f,1.0f,0.0f);

//timing
float deltaTime = 0.0f;
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
	
	float cameraSpeed = static_cast<float>(2.5 * deltaTime);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
}

void render_frame() {
	if (!g_window)
		return;
	glfwPollEvents();
	glClear(GL_COLOR_BUFFER_BIT);
	glfwSwapBuffers(g_window);
}

void pipelineTransform(Shader &shader) 
{
	glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
	glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, 0.1f, 100.0f);

	shader.setMat4("view", view);
	shader.setMat4("projection", projection);

	for(int x = -25; x < 25; x++)
	{
		for(int z = -25; z < 25; z++)
		{
			glm::mat4 model = glm::mat4(1.0f);
			model = glm::translate(model, glm::vec3(x, -1.0f, z));
			shader.setMat4("model", model);

			// Draw the cube
			glDrawArrays(GL_TRIANGLES, 0, 36);
		}
	}
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


	Shader shader("src/shader/3.3cube.vs", "src/shader/3.3cube.fs");

	float vertices[] = {
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f, // 0 postion (vec3) 1 texCoord (vec2) 2 color (vec3)
		 0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		-0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		-0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

		 0.5f,  0.5f,  0.5f,  1.0f,0.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
		-0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
		-0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
		 0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		 0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
		-0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
		-0.5f,  0.5f, -0.5f,  0.0f, 1.0f
	};

	unsigned int indices[] = {
			0, 1, 3, // First triangle
			1, 2, 3  // Second triangle
	};


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

	// LOAD AND CREATE A TEXTURE

	unsigned int texture1, texture2;

	// Texture 1
	glGenTextures(1, &texture1);
	glBindTexture(GL_TEXTURE_2D, texture1);

	// wrapping parameter
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
									GL_REPEAT); // s t r -> (x,y,z)
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// set texture filtering parameters
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// load image, create texture and generate mipmaps

	int width, height, nrChannels;
	stbi_set_flip_vertically_on_load(true); // tell stb_image.h to flip loaded texture's on the y-axis

	unsigned char *data = stbi_load(("assets/container.jpg"), &width, &height, &nrChannels, 0);

	printf("stbi_load returned %p (w=%d h=%d c=%d)\n", (void *)data, width, height, nrChannels);
	if (data) 
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	} 
	else
		std::cout << "Failed to load texture" << std::endl;

	stbi_image_free(data);
	data = nullptr;

	/*
	//Texture 2

	glGenTexture(1, &texture2);
	glBindTexture(GL_TEXTURE_2D, texture2);

	//wraping
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	//texture filtering

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	//load data

	*/

	shader.use(); // don't forget to activate/use the shader before setting
								// uniforms!

	shader.setInt("texture1", 0); 
	// replace ->
	//glUniform1i(glGetUniformLocation(shader.ID, "texture1"), 0);

	// or set it via the texture class ourShader.setInt("texture1",0);

	// Render loop
	while (!glfwWindowShouldClose(window)) 
	{
		//per-frame time logic 
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;
		
		// Imput
		processInput(window);

		// rendering commands here
		//glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// bind texture on corresponding texture units
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture1);


		// shader.setFloat("someUniform", 1.0f);
		// Draw
		
		//shader.use();
		glBindVertexArray(VAO);
		pipelineTransform(shader);
		//glBindVertexArray(VAO);
		//glm::mat4 transform = glm::mat4(1.0f);
		// Configurer la caméra (View & Projection) via notre fonction
		// pipelineTransform(shader.ID);

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
