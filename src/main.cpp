#include "camera/camera.hpp"
#include "model/model.hpp"

#define SDL_MAIN_HANDLED
#include "SDL/SDL.h"

#include "glad/glad.h"

#include <cstdint>
#include <chrono>
#include <iostream>
#include <unordered_map>

// temp
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl2.h"

// for compileShader()
#include <string>
#include <fstream>
#include <sstream>

#include <unordered_map>

GLuint compileShader(const std::string& filename, GLenum type)
{
	std::ifstream inputStream{ filename };

	std::stringstream stringStream{};
	stringStream << inputStream.rdbuf();

	std::string srcStr{ stringStream.str() };
	const char* srcCStr{ srcStr.c_str() };

	GLuint shader{ glCreateShader(type) };
	glShaderSource(shader, 1, &srcCStr, nullptr);
	glCompileShader(shader);

	GLint success{};
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[1024]{};
		glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
		std::cerr << infoLog << '\n';
	}

	return shader;
}

struct ShaderProgram
{
	std::string vsPath{};
	std::string fsPath{};
	GLuint program{};
};

void linkShaderProgram(ShaderProgram& shaderProgram)
{
	auto vertexShader{ compileShader(shaderProgram.vsPath, GL_VERTEX_SHADER) };
	auto fragmentShader{ compileShader(shaderProgram.fsPath, GL_FRAGMENT_SHADER) };

	shaderProgram.program = glCreateProgram();
	glAttachShader(shaderProgram.program, vertexShader);
	glAttachShader(shaderProgram.program, fragmentShader);
	glLinkProgram(shaderProgram.program);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}

void message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* message, void const* user_param)
{
	auto const src_str = [source]() {
		switch (source)
		{
		case GL_DEBUG_SOURCE_API: return "API";
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "WINDOW SYSTEM";
		case GL_DEBUG_SOURCE_SHADER_COMPILER: return "SHADER COMPILER";
		case GL_DEBUG_SOURCE_THIRD_PARTY: return "THIRD PARTY";
		case GL_DEBUG_SOURCE_APPLICATION: return "APPLICATION";
		case GL_DEBUG_SOURCE_OTHER: return "OTHER";
		}
		}();

		auto const type_str = [type]() {
			switch (type)
			{
			case GL_DEBUG_TYPE_ERROR: return "ERROR";
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED_BEHAVIOR";
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UNDEFINED_BEHAVIOR";
			case GL_DEBUG_TYPE_PORTABILITY: return "PORTABILITY";
			case GL_DEBUG_TYPE_PERFORMANCE: return "PERFORMANCE";
			case GL_DEBUG_TYPE_MARKER: return "MARKER";
			case GL_DEBUG_TYPE_OTHER: return "OTHER";
			}
			}();

			auto const severity_str = [severity]() {
				switch (severity) {
				case GL_DEBUG_SEVERITY_NOTIFICATION: return "NOTIFICATION";
				case GL_DEBUG_SEVERITY_LOW: return "LOW";
				case GL_DEBUG_SEVERITY_MEDIUM: return "MEDIUM";
				case GL_DEBUG_SEVERITY_HIGH: return "HIGH";
				}
				}();
				std::cout << src_str << ", " << type_str << ", " << severity_str << ", " << id << ": " << message << '\n';
}

struct Stats
{
	float frameTime{};
	int triangleCount{};
	int drawcallCount{};
};

int main()
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		std::cerr << "Failed to initialize SDL2.\n";
		return -1;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

	constexpr int screenWidth{ 1440 };
	constexpr int screenHeight{ 810 };

	SDL_Window* window{ SDL_CreateWindow("Hello world!",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screenWidth, screenHeight, SDL_WINDOW_OPENGL) };
	if (!window)
	{
		std::cerr << "Failed to create window.\n";
		return -1;
	}
	SDL_GLContext glContext{ SDL_GL_CreateContext(window) };
	SDL_GL_MakeCurrent(window, glContext);
	SDL_GL_SetSwapInterval(0);

	std::unordered_map<SDL_Keycode, bool> keyStates{};

	if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
	{
		std::cerr << "Failed to load OpenGL functions.\n";
		return -1;
	}

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
	glDebugMessageCallback(message_callback, nullptr);

	glViewport(0, 0, screenWidth, screenHeight);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui_ImplSDL2_InitForOpenGL(window, glContext);
	ImGui_ImplOpenGL3_Init();

	ModelObject model{ "assets/Bistro1.glb" };
	//ModelObject model{ "assets/Sponza/NewSponza_Main_glTF_002.gltf", "assets/Sponza" };
	//ModelObject model{ "assets/deccer2.glb" };

	float screenQuadVerts[] = {
		// Position				// UV
		-1.0f, -1.0f, 0.0f,		0.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,		1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f,		1.0f, 1.0f,

		 1.0f,  1.0f, 0.0f,		1.0f, 1.0f,
		-1.0f,  1.0f, 0.0f,		0.0f, 1.0f,
		-1.0f, -1.0f, 0.0f,		0.0f, 0.0f
	};

	GLuint screenQuadVBO{};
	glCreateBuffers(1, &screenQuadVBO);
	glNamedBufferStorage(screenQuadVBO, sizeof(float) * 5 * 6, screenQuadVerts, GL_NONE);

	GLuint screenQuadVAO{};
	glCreateVertexArrays(1, &screenQuadVAO);
	glVertexArrayVertexBuffer(screenQuadVAO, 0, screenQuadVBO, 0, sizeof(float) * 5);

	glEnableVertexArrayAttrib(screenQuadVAO, 0);
	glEnableVertexArrayAttrib(screenQuadVAO, 1);

	glVertexArrayAttribFormat(screenQuadVAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribFormat(screenQuadVAO, 1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 3);

	glVertexArrayAttribBinding(screenQuadVAO, 0, 0);
	glVertexArrayAttribBinding(screenQuadVAO, 1, 0);
	
	std::unordered_map<std::string, ShaderProgram> shaderPrograms;

	shaderPrograms["uber"] = { "src/shaders/uber.vert", "src/shaders/uber.frag" };
	shaderPrograms["transparent"] = { "src/shaders/uber.vert", "src/shaders/transparent.frag" };
	shaderPrograms["comp"] = { "src/shaders/comp.vert", "src/shaders/comp.frag" };

	for (auto& [name, shaderProgram] : shaderPrograms)
	{
		linkShaderProgram(shaderProgram);
	}

	GLuint opaqueFBO{};
	glCreateFramebuffers(1, &opaqueFBO);

	GLuint opaqueTexture{};
	glCreateTextures(GL_TEXTURE_2D, 1, &opaqueTexture);
	glTextureStorage2D(opaqueTexture, 1, GL_RGBA16F, screenWidth, screenHeight);
	GLuint depthTexture{};
	glCreateTextures(GL_TEXTURE_2D, 1, &depthTexture);
	glTextureStorage2D(depthTexture, 1, GL_DEPTH_COMPONENT32F, screenWidth, screenHeight);

	glNamedFramebufferTexture(opaqueFBO, GL_COLOR_ATTACHMENT0, opaqueTexture, 0);
	glNamedFramebufferTexture(opaqueFBO, GL_DEPTH_ATTACHMENT, depthTexture, 0);

	GLuint transparentFBO{};
	glCreateFramebuffers(1, &transparentFBO);

	GLuint accumTexture{};
	glCreateTextures(GL_TEXTURE_2D, 1, &accumTexture);
	glTextureStorage2D(accumTexture, 1, GL_RGBA16F, screenWidth, screenHeight);
	GLuint revealTexture{};
	glCreateTextures(GL_TEXTURE_2D, 1, &revealTexture);
	glTextureStorage2D(revealTexture, 1, GL_R8, screenWidth, screenHeight);

	glNamedFramebufferTexture(transparentFBO, GL_COLOR_ATTACHMENT0, accumTexture, 0);
	glNamedFramebufferTexture(transparentFBO, GL_COLOR_ATTACHMENT1, revealTexture, 0);
	glNamedFramebufferTexture(transparentFBO, GL_DEPTH_ATTACHMENT, depthTexture, 0);
	
	GLenum drawBuffers[]{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glNamedFramebufferDrawBuffers(transparentFBO, 2, drawBuffers);

	//if (glCheckNamedFramebufferStatus(transparentFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	//	std::cerr << "framebuffer error\n";

	Camera camera({ 0.0f, 0.0f, -10.0f }, { 0.0f, 180.0f });

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	double lastTime{ SDL_GetTicks64() * 0.001 };

	Stats stats{};

	char selectedProgram[512]{};

	bool quit{ false };
	while (!quit)
	{
		const double currentTime{ SDL_GetTicks64() * 0.001 };
		const float deltaTime{ static_cast<float>(SDL_GetTicks64() * 0.001 - lastTime) };
		lastTime = currentTime;

		SDL_Event e{};
		while (SDL_PollEvent(&e) != 0)
		{
			ImGui_ImplSDL2_ProcessEvent(&e);

			if (e.type == SDL_QUIT)
			{
				quit = true;
			}
			else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
			{
				keyStates[e.key.keysym.sym] = (e.type == SDL_KEYDOWN);
			}
		}

		// Using operator[] is fine since the state will default to false
		constexpr float moveSpeed{ 5.0f };
		constexpr float lookSpeed{ 90.0f };
		glm::vec3 displacement{};
		if (keyStates[SDLK_w])
		{
			displacement.z -= moveSpeed * deltaTime;
		}
		if (keyStates[SDLK_s])
		{
			displacement.z += moveSpeed * deltaTime;
		}
		if (keyStates[SDLK_a])
		{
			displacement.x -= moveSpeed * deltaTime;
		}
		if (keyStates[SDLK_d])
		{
			displacement.x += moveSpeed * deltaTime;
		}
		if (keyStates[SDLK_e])
		{
			displacement.y += moveSpeed * deltaTime;
		}
		if (keyStates[SDLK_q])
		{
			displacement.y -= moveSpeed * deltaTime;
		}

		if (keyStates[SDLK_UP])
		{
			camera.mRot.x += lookSpeed * deltaTime;
		}
		if (keyStates[SDLK_DOWN])
		{
			camera.mRot.x -= lookSpeed * deltaTime;
		}
		if (keyStates[SDLK_LEFT])
		{
			camera.mRot.y -= lookSpeed * deltaTime;
		}
		if (keyStates[SDLK_RIGHT])
		{
			camera.mRot.y += lookSpeed * deltaTime;
		}

		camera.move(displacement);

		auto start{ std::chrono::system_clock::now() };

		glm::mat4 view{ camera.getViewMatrix() };
		auto p{ glm::perspective(90.0f, 16.0f / 9.0f, 0.25f, 10000.0f) };
		auto tp{ p * view };

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Stats");
		ImGui::Text("frametime %f ms", stats.frameTime);
		ImGui::Text("triangles %i", stats.triangleCount);
		ImGui::Text("draws %i", stats.drawcallCount);
		ImGui::InputText("shader program:", selectedProgram, 512);
		if (ImGui::Button("hot reload"))
		{
			if (auto found{ shaderPrograms.find(std::string{ selectedProgram }) }; found != shaderPrograms.end())
			{
				glDeleteProgram(found->second.program);
				linkShaderProgram(found->second);
			}
			else
			{
				std::cerr << "Shader program \"" << selectedProgram << "\" not found.\n";
			}
		}
		ImGui::End();

		stats.triangleCount = 0;
		stats.drawcallCount = 0;
		
		{
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
			glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

			glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			glUseProgram(shaderPrograms.at("uber").program);
			auto loc{ glGetUniformLocation(shaderPrograms.at("uber").program, "transform") };
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
			loc = { glGetUniformLocation(shaderPrograms.at("uber").program, "view") };
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
			loc = { glGetUniformLocation(shaderPrograms.at("uber").program, "camPos") };
			glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));

			model.draw(glm::translate(glm::mat4{ 1.0f }, { 0.0f, 0.0f, 0.0f }), 
				shaderPrograms.at("uber").program, 
				static_cast<ModelObject::AlphaMode>(ModelObject::OPAQUE | ModelObject::MASK),
				stats.triangleCount, stats.drawcallCount);
			
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND);
			glBlendFunci(0, GL_ONE, GL_ONE);
			glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
			glBlendEquation(GL_FUNC_ADD);

			float color0[]{ 0.0f, 0.0f, 0.0f, 0.0f };
			float color1[]{ 1.0f, 1.0f, 1.0f, 1.0f };
			glBindFramebuffer(GL_FRAMEBUFFER, transparentFBO);
			glClearNamedFramebufferfv(transparentFBO, GL_COLOR, 0, color0);
			glClearNamedFramebufferfv(transparentFBO, GL_COLOR, 1, color1);

			glUseProgram(shaderPrograms.at("transparent").program);
			loc = { glGetUniformLocation(shaderPrograms.at("transparent").program, "transform") };
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
			loc = { glGetUniformLocation(shaderPrograms.at("transparent").program, "view") };
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
			loc = { glGetUniformLocation(shaderPrograms.at("transparent").program, "camPos") };
			glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));
			
			model.draw({ 1.0f }, shaderPrograms.at("transparent").program, ModelObject::BLEND,
				stats.triangleCount, stats.drawcallCount);

			glDepthFunc(GL_ALWAYS);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO);

			glUseProgram(shaderPrograms.at("comp").program);

			glBindTextureUnit(0, accumTexture);
			glBindTextureUnit(1, revealTexture);

			glBindVertexArray(screenQuadVAO);

			glDrawArrays(GL_TRIANGLES, 0, 6);
		}

		glBlitNamedFramebuffer(opaqueFBO, 0, 0, 0, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		SDL_GL_SwapWindow(window);

		auto end{ std::chrono::system_clock::now() };
		auto elapsed{ std::chrono::duration_cast<std::chrono::microseconds>(end - start) };
		stats.frameTime = elapsed.count() / 1000.0f;
	}

	for (auto& [name, shaderProgram] : shaderPrograms)
	{
		glDeleteProgram(shaderProgram.program);
	}

	glDeleteFramebuffers(1, &opaqueFBO);
	glDeleteFramebuffers(1, &transparentFBO);
	
	glDeleteTextures(1, &opaqueTexture);
	glDeleteTextures(1, &accumTexture);
	glDeleteTextures(1, &revealTexture);
	glDeleteTextures(1, &depthTexture);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	SDL_Quit();

	return 0;
}