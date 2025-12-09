#include <config.h>
#include <iostream>
#include <Shader.h>

// If building for Emscripten prefer the compiler-provided macro __EMSCRIPTEN__
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
#include <GLFW/glfw3.h>
#include <emscripten/emscripten.h>
#else
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "errorReporting.h"
#endif

const unsigned int SCREEN_WIDTH = 800;
const unsigned int SCREEN_HEIGHT = 600;

// global window for render callback (WASM uses callback-based main loop)
static GLFWwindow* g_window = nullptr;

bool initGlfw()
{

	std::cout << "INITIALISATION de GLFW" <<std::endl;
	if(!glfwInit())
	{
		std::cout << "LA fenentre glfw n'a pas pu etre initialisé !" << std::endl;
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	return true;
}

void frambuffer_size_callback(GLFWwindow *window, int width, int height)
{
	glViewport(0,0,width,height); // Opengl transforme les coordonées 2D qu'il calacule en coord écran 
	// Ex : (-0.5,0.5) (200, 450) (-1,1) 
}


void processImput(GLFWwindow *window)
{
	if(!window)
		return;

	if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);
}

void render_frame()
{
	if(!g_window)
		return;
	glfwPollEvents();
	glClear(GL_COLOR_BUFFER_BIT);
	glfwSwapBuffers(g_window);

}
/*
const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
    "}\0";
const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
    "}\n\0";

const char *vertexShaderSourceNew ="#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aColor;\n"
    "out vec3 ourColor;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos, 1.0);\n"
    "   ourColor = aColor;\n"
    "}\0";

const char *fragmentShaderSourceNew = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec3 ourColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(ourColor, 1.0f);\n"
    "}\n\0";
*/
int main()
{
    GLFWwindow *window = g_window;
	

	if(!initGlfw())
		return -1;
	
    /*
	if(!glfwInit())
    {
        std::cout << "La fenetre glfw n'a pas pu etre inintialisé !" << std::endl;
		return -1;
    }
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
	*/
    //std::cout << "Hello, World !" <<std::endl;
	window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_WIDTH, "VoxPlace", NULL, NULL);
#if defined(__EMSCRIPTEN__) || defined(EMSCRIPTEN_WEB)
		// store global window for the emscripten callback
		g_window = window;
		if (!g_window) {
			std::cout << "La fenetre n'as pas pu etre créer !" << std::endl;
			glfwTerminate();
			return -1;
		}
		glfwMakeContextCurrent(g_window);
		// On WASM we usually don't need glad; setup viewport and clear color
		glViewport(0,0,SCREEN_WIDTH,SCREEN_HEIGHT);
		glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		emscripten_set_main_loop(render_frame, 0, 1);
#else


	if(!window)
	{
		std::cout << "La fenetre n'as pas pu etre créer !" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, frambuffer_size_callback);

	//Glad gere les pointeures de fonctions OpenGL
		// 	Nous passons a GLAD la fonction de recupération des pointeurs de fonctions OpenGL 
	if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Initialisation de glad echouéee !"<< std::endl;
		glfwTerminate();
		return -1;
	}

	#if defined(HAVE_GLREPORT) && defined(ENABLE_GL_DEBUG) && !defined(__EMSCRIPTEN__)
    enableReportGlErrors();
	#endif
	

	// Pipeline shader : compile les shaders programmes ram -> gpu

		std::cout << "GLAD initialized successfully" << std::endl;
		std::cout << "OpenGL Renderer: " << glGetString(GL_RENDERER) << std::endl;
		std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
	#if defined(HAVE_GLREPORT) && defined(ENABLE_GL_DEBUG) && !defined(__EMSCRIPTEN__) && !defined(EMSCRIPTEN_WEB)
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
		std::cout << "Erreur Linking du shader program \n" << logs <<  std::endl;
	}

// Optional: call GlReportGleeror("after shader link") if implemented in errorReporting

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	//setup vertex data
	*/

	Shader shader("src/shader/3.3shader.vs", "src/shader/3.3shader.fs");

	float vertices[] = {
    // positions         // colors
     0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,   // bottom right
    -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,   // bottom left
     0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f    // top 
	};

	unsigned int indices[] = {
		0, 1, 3, // First triangle
		//1, 2, 3  // Second triangle
	};

	unsigned int VBO, VAO, EBO; // VBO (vertex buffer object) VAO (vertex array object) EBO (element buffer object)

	glGenVertexArrays(1, &VAO);
	glGenBuffers(1, &VBO);
	//glGenBuffers(1, &EBO);
	// bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
	glBindVertexArray(VAO);

	// VBO
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER,sizeof(vertices), vertices, GL_STATIC_DRAW);

	// EBO (bind as ELEMENT_ARRAY_BUFFER while VAO is bound)
	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	//glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices), indices, GL_STATIC_DRAW);
// Optional: call GlReportGleeror("buffers setup") if implemented in errorReporting

	// Vertex attributes (layout location = 0)
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Color attributes (layout location = 1)
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);


	//glBindBuffer(GL_ARRAY_BUFFER,  0);

	// You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
    // VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    //glBindVertexArray(0); 

	//uncomment this call to draw in wireframe polygons.
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//glUseProgram(shaderProgram);
	// RRender loop 
	while(!glfwWindowShouldClose(window))
	{
		//Imput
		processImput(window);
		
		//rendering commands here
		//glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		shader.use();
		//shader.setFloat("someUniform", 1.0f);
		//Draw
		//glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0,3);
		// Rectangle -> DrawElements(GL_TRIANGLES, 6 , GL_UNSIGNED_INT, 0);
		// Optional: call GlReportGleeror("after draw") if implemented in errorReporting
		//check and call events and swap the buffers 
		glfwSwapBuffers(window); // Swap lower to front buffer
		glfwPollEvents();
	}
	/*
	glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
	while(!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		glClear(GL_COLOR_BUFFER_BIT);
		glfwSwapBuffers(window);
	}
	*/


	glfwTerminate();
    return 0;
#endif
}
