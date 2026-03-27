#ifndef CLIENT_RENDER_RENDER_FRAME_CONTEXT_H
#define CLIENT_RENDER_RENDER_FRAME_CONTEXT_H

#include <Frustum.h>

#include <glm/mat4x4.hpp>

struct RenderFrameContext
{
	int framebufferWidth = 1;
	int framebufferHeight = 1;
	float farPlane = 64.0f;
	float fogStart = 0.0f;
	float fogEnd = 64.0f;
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 view = glm::mat4(1.0f);
	Frustum frustum;
};

#endif
