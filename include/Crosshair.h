#pragma once

// ════════════════════════════════════════════════════════════════════
// Crosshair — Minecraft-style textured crosshair
//
// Charge une texture PNG (assets/crosshair.png) et la rend au centre
// de l'écran avec le blending d'inversion (GL_ONE_MINUS_DST_COLOR).
// La taille s'adapte automatiquement à la résolution de la fenêtre.
//
// Usage :
//   Crosshair crosshair;
//   crosshair.init("assets/crosshair.png");  // une seule fois
//   crosshair.render(window);                 // chaque frame
// ════════════════════════════════════════════════════════════════════

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"

class Crosshair
{
public:
	bool visible = true;

	// ── init ──
	// Charge la texture et compile le shader.
	// Appeler UNE SEULE FOIS après l'init OpenGL.
	void init(const char *texturePath)
	{
		// ── Charger la texture ──
		int channels = 0;
		stbi_set_flip_vertically_on_load(true);
		unsigned char *data = stbi_load(texturePath, &texW, &texH, &channels, 4);
		if (data)
		{
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0,
						 GL_RGBA, GL_UNSIGNED_BYTE, data);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			stbi_image_free(data);
			std::cout << "Crosshair loaded: " << texW << "x" << texH << std::endl;
		}
		else
		{
			std::cerr << "ERROR: failed to load crosshair: " << texturePath << std::endl;
			texW = 15;
			texH = 15;
		}

		// ── Shader minimal : quad texturé ──
		const char *vtxSrc =
			"#version 460 core\n"
			"layout(location=0) in vec2 aPos;\n"
			"layout(location=1) in vec2 aUV;\n"
			"out vec2 vUV;\n"
			"void main() {\n"
			"  vUV = aUV;\n"
			"  gl_Position = vec4(aPos, 0.0, 1.0);\n"
			"}\n";
		const char *frgSrc =
			"#version 460 core\n"
			"in vec2 vUV;\n"
			"out vec4 FragColor;\n"
			"uniform sampler2D uTex;\n"
			"void main() {\n"
			"  vec4 c = texture(uTex, vUV);\n"
			"  if (c.a < 0.1) discard;\n"
			"  FragColor = vec4(1.0, 1.0, 1.0, c.a);\n"
			"}\n";

		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vs, 1, &vtxSrc, nullptr);
		glCompileShader(vs);

		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fs, 1, &frgSrc, nullptr);
		glCompileShader(fs);

		program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);

		texLoc = glGetUniformLocation(program, "uTex");

		// ── VAO/VBO pour le quad (6 vertices, stride=4 floats) ──
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

		// position (vec2)
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
		// uv (vec2)
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
							 (void *)(2 * sizeof(float)));

		glBindVertexArray(0);
		initialized = true;
	}

	// ── render ──
	// Dessine le crosshair au centre de la fenêtre.
	// La taille s'adapte au framebuffer via guiScale.
	// Appeler chaque frame dans la boucle de rendu.
	void render(GLFWwindow *window)
	{
		if (!visible || !initialized)
			return;

		// Récupérer la taille du framebuffer à chaque frame
		// → gère le resize de la fenêtre automatiquement
		int fbW = 0;
		int fbH = 0;
		glfwGetFramebufferSize(window, &fbW, &fbH);
		if (fbW <= 0 || fbH <= 0)
			return;

		// guiScale adaptatif : même logique que Minecraft
		// 360p→1, 720p→2, 1080p→3, 1440p→4
		int guiScale = std::max(1, fbH / 360);

		// Taille du quad en pixels, puis conversion en NDC
		float pixelW = static_cast<float>(texW * guiScale);
		float pixelH = static_cast<float>(texH * guiScale);
		float halfW = pixelW / static_cast<float>(fbW);
		float halfH = pixelH / static_cast<float>(fbH);

		// 6 vertices : 2 triangles, format {posX, posY, u, v}
		float verts[24] = {
			-halfW, -halfH, 0.0f, 0.0f,
			 halfW, -halfH, 1.0f, 0.0f,
			 halfW,  halfH, 1.0f, 1.0f,
			-halfW, -halfH, 0.0f, 0.0f,
			 halfW,  halfH, 1.0f, 1.0f,
			-halfW,  halfH, 0.0f, 1.0f,
		};

		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

		// ── Blending inversion Minecraft ──
		// GL_ONE_MINUS_DST_COLOR : inverse les couleurs du fond
		// fond clair → croix sombre, fond sombre → croix claire
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
		glDisable(GL_DEPTH_TEST);

		glUseProgram(program);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glUniform1i(texLoc, 0);

		glBindVertexArray(vao);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// Restaurer l'état
		glBindVertexArray(0);
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	}

	// ── cleanup ──
	void cleanup()
	{
		if (vao)
		{
			glDeleteVertexArrays(1, &vao);
			glDeleteBuffers(1, &vbo);
			glDeleteTextures(1, &texture);
			glDeleteProgram(program);
			vao = 0;
		}
	}

private:
	bool initialized = false;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint program = 0;
	GLuint texture = 0;
	GLint texLoc = -1;
	int texW = 0;
	int texH = 0;
};
