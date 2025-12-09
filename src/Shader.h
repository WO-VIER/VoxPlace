#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
//#include <GLFW/glfw3.h>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>


class Shader
{
public:
    // PRogramm ID
    unsigned int ID;

    // Constructeur qui lit et construit le shader
    Shader(const GLchar *vertexPath, const GLchar *fragmentPath)
    {
        // 1. Récupérer le code du vertex / fragment shader depuis filePath
        std::string vertexCode;
        std::string fragmentCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        const char *vShaderCode = NULL;
        const char *fShaderCode = NULL;
        unsigned int vertex = 0;
        unsigned int fragment = 0;



        // s'surer que les objets ifstream peuvent lancer des exceptions
        vShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions (std::ifstream::failbit | std::ifstream::badbit);

        try
        {

            std::stringstream vShaderStream;
            std::stringstream fShaderStream;
            // ouverture des fichiers
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            
            // lectured des fichiers et places le contenu dans les streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            
            vShaderFile.close();
            fShaderFile.close();

            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch(std::ifstream::failure e)
        {
            std::cout << "ERROR::SHADER:: FILE NOT READ" << std::endl;
        }

        vShaderCode = vertexCode.c_str();
        fShaderCode = fragmentCode.c_str();

        // 2. Compilation des shaders


        // Vertex Shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");

        // Fragment Shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        
        // Shader program
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");

        // Suppression des shaders compilés car ils ne sont plus nécessaires
		glDeleteShader(vertex);
		glDeleteShader(fragment);
    }

    // Utiliser/activer le shader
    void use()
	{
		glUseProgram(ID);
	}

    // Fonctions utilitaires pour configurer les uniformes de shader
    void setBool(const std::string &name, bool value) const
	{
		glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
	}
    void setInt(const std::string &name, int value) const
	{
		glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
	}
    void setFloat(const std::string &name, float value) const
	{
		glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
	}

private:

    // Fonction de vérification de shader compilation/linking erreurs.

    void checkCompileErrors(unsigned int shader, std::string type)
    {
        int sucess;
        char logs[1024];
        
        if(type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &sucess);
            if(!sucess)
            {
                glGetShaderInfoLog(shader, sizeof(logs),NULL, logs);
                std::cout << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << logs << "\n -- --------------------------------------------------- -- " << std::endl;            
            }
        }
        else
        {
            glGetProgramiv(shader , GL_LINK_STATUS, &sucess);
            if(!sucess)
            {
                glGetShaderInfoLog(shader, sizeof(logs),NULL, logs);
                std::cout << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << logs << "\n -- --------------------------------------------------- -- " << std::endl;
            }
        }
    }
};

#endif