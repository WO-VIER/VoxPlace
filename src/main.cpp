#include <config.h>

int main()
{
	
    GLFWwindow *window;

    if(!glfwInit())
    {
        std::cout << "La fenetre glfw n'a pas pu etre inintialisé !" << std::endl;
    }
    //std::cout << "Hello, World !" <<std::endl;
    window = glfwCreateWindow(800, 600, "VoxPlace", NULL, NULL);
	glfwMakeContextCurrent(window);

	if(!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		glfwTerminate();
		return -1;
	}
	glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
	while(!glfwWindowShouldClose(window))
	{
		glfwPollEvents();
		glClear(GL_COLOR_BUFFER_BIT);
		glfwSwapBuffers(window);
	}
	glfwTerminate();
    return 0;
}
