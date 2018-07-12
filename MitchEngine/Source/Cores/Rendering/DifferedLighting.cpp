#include "DifferedLighting.h"
#include "Components/Debug/DebugCube.h"
#include "Components/Transform.h"
#include "Components/Lighting/Light.h"
#include "Engine/ComponentFilter.h"
#include "Utility/Logger.h"
#include "Engine/Window.h"
#include "Graphics/Shader.h"
#include "Engine/Resource.h"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/quaternion.hpp>

#include "Components/Camera.h"
#include <iostream>

DifferedLighting::DifferedLighting() : Base(ComponentFilter().Requires<Transform>().Requires<Light>())
{
}

// renderQuad() renders a 1x1 XY quad in NDC
// -----------------------------------------
unsigned int quadVAO = 0;
unsigned int quadVBO;
void renderQuad()
{
	if (quadVAO == 0)
	{
		float quadVertices[] = {
			// positions        // texture Coords
			-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
			-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
			1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
			1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		};
		// setup plane VAO
		glGenVertexArrays(1, &quadVAO);
		glGenBuffers(1, &quadVBO);
		glBindVertexArray(quadVAO);
		glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	}
	glBindVertexArray(quadVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void DifferedLighting::Init()
{
	LightingPassShader = new Shader("Assets/Shaders/DifferedLighting.vert", "Assets/Shaders/DifferedLighting.frag");

	float SCR_WIDTH = (float)Window::WINDOW_WIDTH;
	float SCR_HEIGHT = (float)Window::WINDOW_HEIGHT;

	glGenFramebuffers(1, &gBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);

	// position color buffer
	glGenTextures(1, &gPosition);
	glBindTexture(GL_TEXTURE_2D, gPosition);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gPosition, 0);

	// normal color buffer
	glGenTextures(1, &gNormal);
	glBindTexture(GL_TEXTURE_2D, gNormal);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGB, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormal, 0);

	glGenTextures(1, &gAlbedoSpec);
	glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCR_WIDTH, SCR_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, gAlbedoSpec, 0);

	unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
	glDrawBuffers(3, attachments);

	unsigned int rboDepth;
	glGenRenderbuffers(1, &rboDepth);
	glBindRenderbuffer(GL_RENDERBUFFER, rboDepth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, SCR_WIDTH, SCR_HEIGHT);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepth);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cout << "Framebuffer not complete!" << std::endl;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// shader configuration
	// --------------------
	LightingPassShader->Use();
	LightingPassShader->SetInt("gPosition", 0);
	LightingPassShader->SetInt("gNormal", 1);
	LightingPassShader->SetInt("gAlbedoSpec", 2);

}

void DifferedLighting::Update(float dt)
{
}

DifferedLighting::~DifferedLighting()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "Renderer Destroyed...");
}

void DifferedLighting::PreRender()
{
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glBindFramebuffer(GL_FRAMEBUFFER, gBuffer);
}

void DifferedLighting::PostRender()
{
	Camera* CurrentCamera = Camera::CurrentCamera;
	if (!CurrentCamera)
	{
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	float SCR_WIDTH = (float)Window::WINDOW_WIDTH;
	float SCR_HEIGHT = (float)Window::WINDOW_HEIGHT;

	glm::mat4 projection = glm::perspective(glm::radians(Camera::CurrentCamera->Zoom), SCR_WIDTH / SCR_HEIGHT, 0.1f, 100.0f);
	glm::mat4 view = Camera::CurrentCamera->GetViewMatrix();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	LightingPassShader->Use();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gPosition);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, gNormal);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, gAlbedoSpec);

	LightingPassShader->Use();
	GLuint Program = LightingPassShader->GetProgram();

	LightingPassShader->SetMat4("projection", projection);

	LightingPassShader->SetMat4("view", view);

	int i = 0;
	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		Transform& TransformComponent = InEntity.GetComponent<Transform>();
		Light& LightComponent = InEntity.GetComponent<Light>();

		// light

		LightingPassShader->SetVec3("lights[" + std::to_string(i) + "].Position", TransformComponent.GetPosition());
		LightingPassShader->SetVec3("lights[" + std::to_string(i) + "].Color", LightComponent.Colour);

		const float Constant = 1.0;
		const float Linear = 0.7;
		const float Quadratic = 1.8;
		LightingPassShader->SetFloat("lights[" + std::to_string(i) + "].Linear", Linear);
		LightingPassShader->SetFloat("lights[" + std::to_string(i) + "].Quadratic", Quadratic);

		const float maxBrightness = std::fmaxf(std::fmaxf(LightComponent.Colour.r, LightComponent.Colour.g), LightComponent.Colour.b);
		float radius = (-Linear + std::sqrt(Linear * Linear - 4 * Quadratic * (Constant - (256.0f / 5.0f) * maxBrightness))) / (2.0f * Quadratic);
		LightingPassShader->SetFloat("lights[" + std::to_string(i) + "].Radius", radius);

		i++;
	}
	LightingPassShader->SetVec3("viewPos", Camera::CurrentCamera->Position);

	renderQuad();


	// send light relevant uniforms
	// 2.5. copy content of geometry's depth buffer to default framebuffer's depth buffer
	// ----------------------------------------------------------------------------------
	glBindFramebuffer(GL_READ_FRAMEBUFFER, gBuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // write to default framebuffer
											   // blit to default framebuffer. Note that this may or may not work as the internal formats of both the FBO and default framebuffer have to match.
											   // the internal formats are implementation defined. This works on all of my systems, but if it doesn't on yours you'll likely have to write to the 		
											   // depth buffer in another shader stage (or somehow see to match the default framebuffer's internal format with the FBO's internal format).
	glBlitFramebuffer(0, 0, SCR_WIDTH, SCR_HEIGHT, 0, 0, SCR_WIDTH, SCR_HEIGHT, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}