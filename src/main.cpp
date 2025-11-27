#include <config.h>

bool initGlfw()
{
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
	glViewport(0,0,width,height);
}

int main()
{
	
    GLFWwindow *window = NULL;
	
	
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
    window = glfwCreateWindow(800, 600, "VoxPlace", NULL, NULL);
	if(!window)
	{
		std::cout << "La fenetre n'as pas pu etre créer !" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		//Glad gere les pointeures de fonctions OpenGL
		// 	Nous passons a GLAD la fonction de recupération des pointeurs de fonctions OpenGL 
		std::cout << "Initialisation de glad echouéee !"<< std::endl;
		glfwTerminate();
		return -1;
	}
	glViewport(0,0,800,600); // Opengl transforme les coordonées 2D qu'il calacule en coord écran 
	// Ex : (-0.5,0.5) (200, 450) (-1,1) 
	
	while(!glfwWindowShouldClose(window))
	{
		glClear(GL_COLOR_BUFFER_BIT);
		glfwSetFramebufferSizeCallback(window, frambuffer_size_callback);
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
}
