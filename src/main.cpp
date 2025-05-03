#include "camera/camera.hpp"
#include "model/model.hpp"
#include "scene/scene.hpp"

#define SDL_MAIN_HANDLED
#include "SDL/SDL.h"

#include "glad/glad.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include "glm/ext/matrix_clip_space.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl2.h"

#include "meshoptimizer/meshoptimizer.h"

#include <cmath> // for cbrt and ceil
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <string>
#include <fstream>
#include <sstream>



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
};



glm::mat4 infiniteReversePerspective(float fovY, float aspect, float zNear)
{
    float f = 1.0f / tan(fovY / 2.0f);
    return glm::mat4(
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, zNear, 0.0f);
}



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
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
    glDebugMessageCallback(message_callback, nullptr);

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init();



    SceneObject sceneObject{};
    sceneObject.mViewCount = 2;

    std::vector<SceneObject::ModelObjectLoadInfo> modelLoadInfos
    {
        //{.name{"bistro"}, .path{ "../../assets/Sponza/Sponza.gltf" }, .directory{ "../../assets/Sponza" } },
        { .name{"bistro"}, .path{ "../../assets/Bistro1.glb" } },
        //{.name{"bistro"}, .path{ "../../assets/deccer2.glb" } },
        //{.name{"bistro"}, .path{ "../../assets/Bistro2.glb" } },
        { .name{"cubes"}, .path{ "../../assets/cubes.glb" } },
    };
    sceneObject.loadModels(modelLoadInfos);
    sceneObject.initGlMemory();

    sceneObject.mShaderPrograms["uber"]             = { "../../src/shaders/uber.vert", "../../src/shaders/uber.frag" };
    sceneObject.mShaderPrograms["transparent"]      = { "../../src/shaders/uber.vert", "../../src/shaders/transparent.frag" };
    sceneObject.mShaderPrograms["comp"]             = { "../../src/shaders/comp.vert", "../../src/shaders/comp.frag" };
    sceneObject.mShaderPrograms["lighting"]         = { "../../src/shaders/comp.vert", "../../src/shaders/lighting.frag" };
    sceneObject.mShaderPrograms["occluder_batch"]   = { .computePath{ "../../src/shaders/occluder_batch.comp" } };
    sceneObject.mShaderPrograms["cluster_batch"]    = { .computePath{ "../../src/shaders/cluster_batch.comp" } };
    sceneObject.mShaderPrograms["depth_downsample"] = { .computePath{ "../../src/shaders/depth_downsample.comp" }};
    sceneObject.mShaderPrograms["shadow"]           = { "../../src/shaders/shadow.vert", "../../src/shaders/shadow.frag" };
    sceneObject.linkShaderPrograms();

    Camera camera({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f });


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



    GLuint opaqueFBO{};
    glCreateFramebuffers(1, &opaqueFBO);

    GLuint opaqueTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &opaqueTexture);
    glTextureStorage2D(opaqueTexture, 1, GL_RGBA16F, screenWidth, screenHeight);
    GLuint normalTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &normalTexture);
    glTextureStorage2D(normalTexture, 1, GL_RGBA16F, screenWidth, screenHeight); // todo: find better formats (after srgb)
    GLuint depthTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &depthTexture);
    glTextureStorage2D(depthTexture, 1, GL_DEPTH_COMPONENT32F, screenWidth, screenHeight);

    glNamedFramebufferTexture(opaqueFBO, GL_COLOR_ATTACHMENT0, opaqueTexture,   0);
    glNamedFramebufferTexture(opaqueFBO, GL_COLOR_ATTACHMENT1, normalTexture,   0);
    glNamedFramebufferTexture(opaqueFBO, GL_DEPTH_ATTACHMENT,  depthTexture,    0);

    GLenum drawBuffersG[]{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glNamedFramebufferDrawBuffers(opaqueFBO, 2, drawBuffersG);

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

    GLuint hiZTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &hiZTexture);
    glTextureStorage2D(hiZTexture, std::floor(std::log2(std::max(screenWidth, screenHeight))) + 1, GL_R32F, screenWidth, screenHeight);
    GLuint64 hiZTexHandle{ glGetTextureHandleARB(hiZTexture) };
    glMakeTextureHandleResidentARB(hiZTexHandle);

    constexpr int shadowMapLength{ 1024 };

    GLuint shadowFBO{};
    glCreateFramebuffers(1, &shadowFBO);

    GLuint shadowMap{};
    glCreateTextures(GL_TEXTURE_2D, 1, &shadowMap);
    glTextureStorage2D(shadowMap, 1, GL_DEPTH_COMPONENT32F, shadowMapLength, shadowMapLength);

    glNamedFramebufferTexture(shadowFBO, GL_DEPTH_ATTACHMENT, shadowMap, 0);

    GLuint shadowHiZ{};
    glCreateTextures(GL_TEXTURE_2D, 1, &shadowHiZ);
    glTextureStorage2D(shadowHiZ, std::floor(std::log2(shadowMapLength)) + 1, GL_R32F, shadowMapLength, shadowMapLength);
    GLuint64 shadowHiZHandle{ glGetTextureHandleARB(shadowHiZ) };
    glMakeTextureHandleResidentARB(shadowHiZHandle);

    double lastTime{ SDL_GetTicks64() * 0.001 };

    Stats stats{};
    bool updateViewFrustum{ true };
    int hiZDisplayLevel{ 0 };

    glm::mat4 hiZView{ 1.0f };

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

        const glm::vec3 lightDirection{ glm::normalize(glm::vec3{ -2.0f, 8.0f, 1.0f }) };
        const glm::mat4 lightView{ glm::lookAt(lightDirection, glm::vec3{ 0.0f }, glm::vec3{0.0f, 1.0f, 0.0f}) };
        const glm::mat4 lightProj{ glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 10.0f, -20.0f) };
        const glm::mat4 lightProjView{ lightProj * lightView };

        glm::mat4 view{ camera.getViewMatrix() };
        auto proj = infiniteReversePerspective(camera.mFov, 16.0f / 9.0f, camera.mZNear);
        auto s{ glm::scale(glm::mat4{ 1.0f }, glm::vec3{ 1.0f }) };
        auto tp{ proj * view };

        if (updateViewFrustum)
        {
            hiZView = view;
        }

        glViewport(0, 0, screenWidth, screenHeight);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Stats");
        ImGui::Text("frametime %f ms", stats.frameTime);
        ImGui::InputText(":shader program", selectedProgram, 512);
        ImGui::InputInt("hi-z level to display", &hiZDisplayLevel);
        if (ImGui::Button("hot reload"))
        {
            if (auto found{ sceneObject.mShaderPrograms.find(std::string{ selectedProgram }) }; found != sceneObject.mShaderPrograms.end())
            {
                glDeleteProgram(found->second.program);
                SceneObject::linkShaderProgram(found->second);
            }
            else
            {
                std::cerr << "Shader program \"" << selectedProgram << "\" not found.\n";
            }
        }
        ImGui::Checkbox("update view frustum", &updateViewFrustum);
        ImGui::End();

        {
            void* map{};

            std::vector<SceneObject::IndirectDraw> indirectDraws(sceneObject.mViewCount);
            map = glMapNamedBuffer(sceneObject.mIndirectDrawBuffers, GL_WRITE_ONLY);
            std::memcpy(map, indirectDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectDraw));
            glUnmapNamedBuffer(sceneObject.mIndirectDrawBuffers);

            Camera::Frustum frustum{ camera.getViewFrustum(proj) };

            sceneObject.mViews[0] =
            {
                .view{ hiZView },
                .proj{ proj },

                .top{ frustum.top },
                .bottom{ frustum.bottom },
                .right{ frustum.right },
                .left{ frustum.left },
                .far{ frustum.far },
                .near{ frustum.near },

                .camPosAndZNear{ camera.mPos, camera.mZNear },
                .hiZ{ hiZTexHandle }
            };

            frustum = Camera::makeViewFrustum(lightProj * lightView);
            sceneObject.mViews[1] =
            {
                .view{ lightView },
                .proj{ lightProj },

                .top{ frustum.top },
                .bottom{ frustum.bottom },
                .right{ frustum.right },
                .left{ frustum.left },
                .far{ frustum.far },
                .near{ frustum.near },

                .camPosAndZNear{ glm::vec3{ 0.0f }, 10.0f },
                .hiZ{ shadowHiZHandle }
            };
            
            if (updateViewFrustum)
            {
                map = glMapNamedBuffer(sceneObject.mViewSsbo, GL_WRITE_ONLY);
                std::memcpy(map, sceneObject.mViews.data(), sceneObject.mViewCount * sizeof(SceneObject::View));
                glUnmapNamedBuffer(sceneObject.mViewSsbo);
            }

            glUseProgram(sceneObject.mShaderPrograms.at("occluder_batch").program);

            auto loc{ glGetUniformLocation(sceneObject.mShaderPrograms.at("occluder_batch").program, "clusterCount") };
            glUniform1ui(loc, sceneObject.mClusterCount);
            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("occluder_batch").program, "viewIndexCount");
            glUniform1ui(loc, sceneObject.mIndexCount);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mIndirectDrawBuffers);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mWriteIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, sceneObject.mVisibilityBitmaskSsbo);

            glDispatchCompute(std::ceil(std::cbrt(sceneObject.mClusterCount)), 
                std::ceil(std::cbrt(sceneObject.mClusterCount)), std::ceil(std::cbrt(sceneObject.mClusterCount) / 64.0f));

            GLsync occluderBatchFence{ glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, GL_NONE) };

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_GREATER);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glClearColor(0.78f, 0.90f, 0.99f, 1.0f);
            glClearDepth(0.0f);

            glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO);
            glViewport(0, 0, screenWidth, screenHeight);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glBindVertexArray(sceneObject.mVao);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sceneObject.mWriteIbo);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sceneObject.mIndirectDrawBuffers);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);

            glUseProgram(sceneObject.mShaderPrograms.at("uber").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));

            glWaitSync(occluderBatchFence, GL_NONE, GL_TIMEOUT_IGNORED);
            glDeleteSync(occluderBatchFence);

            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<void*>(0 * sizeof(SceneObject::IndirectDraw)));

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);

            glUseProgram(sceneObject.mShaderPrograms.at("shadow").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(lightProjView));

            glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
            glViewport(0, 0, shadowMapLength, shadowMapLength);
            glClear(GL_DEPTH_BUFFER_BIT);

            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<void*>(1 * sizeof(SceneObject::IndirectDraw)));

            if (updateViewFrustum)
            {
                glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

                glCopyImageSubData(depthTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                                    hiZTexture,   GL_TEXTURE_2D, 0, 0, 0, 0,
                                    screenWidth, screenHeight, 1);

                GLsync occluderDrawFence{ glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, GL_NONE) };

                glUseProgram(sceneObject.mShaderPrograms.at("depth_downsample").program);

                int hiZWidth{ screenWidth };
                int hiZHeight{ screenHeight };

                glWaitSync(occluderDrawFence, GL_NONE, GL_TIMEOUT_IGNORED);
                glDeleteSync(occluderDrawFence);

                for (int i{ 0 }; i < std::floor(std::log2(std::max(screenWidth, screenHeight))); ++i)
                {
                    glBindImageTexture(0, hiZTexture, i, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
                    glBindImageTexture(1, hiZTexture, i + 1, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

                    hiZWidth /= 2;
                    hiZWidth = hiZWidth > 0 ? hiZWidth : 1;

                    hiZHeight /= 2;
                    hiZHeight = hiZHeight > 0 ? hiZHeight : 1;

                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

                    glDispatchCompute(std::ceil(hiZWidth / 32.0f), hiZHeight, 1);
                }
            }
            else
            {
                glFinish();
            }
            glFinish();
            {
                glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

                glCopyImageSubData(shadowMap, GL_TEXTURE_2D, 0, 0, 0, 0,
                    shadowHiZ, GL_TEXTURE_2D, 0, 0, 0, 0,
                    shadowMapLength, shadowMapLength, 1);

                GLsync occluderDrawFence{ glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, GL_NONE) };

                glUseProgram(sceneObject.mShaderPrograms.at("depth_downsample").program);

                int hiZWidth{ shadowMapLength };
                int hiZHeight{ shadowMapLength };

                for (int i{ 0 }; i < std::floor(std::log2(shadowMapLength)); ++i)
                {
                    glBindImageTexture(0, shadowHiZ, i, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
                    glBindImageTexture(1, shadowHiZ, i + 1, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

                    hiZWidth /= 2;
                    hiZWidth = hiZWidth > 0 ? hiZWidth : 1;

                    hiZHeight /= 2;
                    hiZHeight = hiZHeight > 0 ? hiZHeight : 1;

                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

                    glDispatchCompute(std::ceil(hiZWidth / 32.0f), hiZHeight, 1);
                }
            }
            glFinish();

            // todo: cleanup glViewport, glEnable
            glViewport(0, 0, screenWidth, screenHeight);
            
            map = glMapNamedBuffer(sceneObject.mIndirectDrawBuffers, GL_WRITE_ONLY);
            std::memcpy(map, indirectDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectDraw));
            glUnmapNamedBuffer(sceneObject.mIndirectDrawBuffers);

            map = glMapNamedBuffer(sceneObject.mIndirectBlendDrawBuffers, GL_WRITE_ONLY);
            std::memcpy(map, indirectDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectDraw));
            glUnmapNamedBuffer(sceneObject.mIndirectBlendDrawBuffers);

            glUseProgram(sceneObject.mShaderPrograms.at("cluster_batch").program);

            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("cluster_batch").program, "clusterCount");
            glUniform1ui(loc, sceneObject.mClusterCount);
            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("cluster_batch").program, "viewIndexCount");
            glUniform1ui(loc, sceneObject.mIndexCount);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,  sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,  sceneObject.mIndirectDrawBuffers);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,  sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3,  sceneObject.mWriteIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4,  sceneObject.mIndirectBlendDrawBuffers);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5,  sceneObject.mWriteBlendIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6,  sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8,  sceneObject.mTransformsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  sceneObject.mVisibilityBitmaskSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, sceneObject.mViewSsbo);
            
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

            glDispatchCompute(std::ceil(std::cbrt(sceneObject.mClusterCount)),
                std::ceil(std::cbrt(sceneObject.mClusterCount)), std::ceil(std::cbrt(sceneObject.mClusterCount) / 64.0f));

            GLsync clusterBatchFence{ glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, GL_NONE) };

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_GREATER);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);

            glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO);
            glViewport(0, 0, screenWidth, screenHeight);

            glBindVertexArray(sceneObject.mVao);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sceneObject.mWriteIbo);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sceneObject.mIndirectDrawBuffers);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);

            glUseProgram(sceneObject.mShaderPrograms.at("uber").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));

            glWaitSync(clusterBatchFence, GL_NONE, GL_TIMEOUT_IGNORED);
            glDeleteSync(clusterBatchFence);

            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);

            glUseProgram(sceneObject.mShaderPrograms.at("shadow").program);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(lightProjView));

            glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
            glViewport(0, 0, shadowMapLength, shadowMapLength);
            //glClear(GL_DEPTH_BUFFER_BIT);

            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, reinterpret_cast<void*>(1 * sizeof(SceneObject::IndirectDraw)));

            glViewport(0, 0, screenWidth, screenHeight);

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

            glUseProgram(sceneObject.mShaderPrograms.at("transparent").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("transparent").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("transparent").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("transparent").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sceneObject.mWriteBlendIbo);
            glBindVertexArray(sceneObject.mBlendVao);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sceneObject.mIndirectBlendDrawBuffers);

            glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);

            glDepthFunc(GL_ALWAYS);
            glDisable(GL_BLEND);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glUseProgram(sceneObject.mShaderPrograms.at("lighting").program);

            glBindTextureUnit(0, opaqueTexture);
            glBindTextureUnit(1, normalTexture);

            // temp
            glBindTextureUnit(2, shadowMap);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "hiZLevel") };
            glUniform1i(loc, hiZDisplayLevel);

            glBindVertexArray(screenQuadVAO);

            glDrawArrays(GL_TRIANGLES, 0, 6);
            
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glUseProgram(sceneObject.mShaderPrograms.at("comp").program);

            glBindTextureUnit(0, accumTexture);
            glBindTextureUnit(1, revealTexture);

            glBindVertexArray(screenQuadVAO);

            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);

        auto end{ std::chrono::system_clock::now() };
        auto elapsed{ std::chrono::duration_cast<std::chrono::microseconds>(end - start) };
        stats.frameTime = elapsed.count() / 1000.0f;
    }

    glDeleteFramebuffers(1, &opaqueFBO);
    glDeleteFramebuffers(1, &transparentFBO);
    glDeleteFramebuffers(1, &shadowFBO);
    
    glDeleteTextures(1, &opaqueTexture);
    glDeleteTextures(1, &accumTexture);
    glDeleteTextures(1, &revealTexture);
    glDeleteTextures(1, &depthTexture);
    glDeleteTextures(1, &hiZTexture);
    glDeleteTextures(1, &shadowMap);
    glDeleteTextures(1, &shadowHiZ);


    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();

    return 0;
}