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
#include "glm/gtx/extended_min_max.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl2.h"

#include "meshoptimizer/meshoptimizer.h"

#include "stb/stb_image.h"

#include <array>
#include <cmath> // for cbrt, floor, and ceil
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
    float f = 1.0f / std::tan(fovY / 2.0f);
    return glm::mat4(
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, zNear, 0.0f);
}



std::array<glm::vec3, 8> getWorldSpaceFrustumCorners(const glm::mat4& proj, const glm::mat4& view)
{
    std::array<glm::vec3, 8> corners
    {
        glm::vec3{ -1.0f,  1.0f, 1.0f },
        glm::vec3{  1.0f,  1.0f, 1.0f },
        glm::vec3{  1.0f, -1.0f, 1.0f },
        glm::vec3{ -1.0f, -1.0f, 1.0f },
        glm::vec3{ -1.0f,  1.0f,  1.0f },
        glm::vec3{  1.0f,  1.0f,  1.0f },
        glm::vec3{  1.0f, -1.0f,  1.0f },
        glm::vec3{ -1.0f, -1.0f,  1.0f },
    }; 

    const auto inv{ glm::inverse(proj * view) };

    for (auto& corner : corners)
    {
        glm::vec4 corner4{ corner, 1.0f };
        corner4 = inv * corner4;
        corner = glm::vec3{ corner4 / corner4.w };
        //corner = glm::vec3{ corner4 };
    }

    return corners;
}

glm::vec3 getFrustumCenterFromCorners(const std::array<glm::vec3, 8>& corners)
{
    glm::vec3 center{ 0.0f };

    for (const auto& corner : corners)
    {
        center += corner;
    }

    return center / 8.0f;
}

struct ViewProj
{
    glm::mat4 view{};
    glm::mat4 proj{};
    float zNear{};
    float zFar{};
    float cascadeRadius{};
};

// Heavily based on https://alextardif.com/shadowmapping.html
ViewProj calculateLightMatricesForCascade(const glm::mat4& cascadeProj, const glm::mat4& cascadeView,
    const glm::vec3& lightDir, int shadowMapLength)
{
    auto frustumCorners{ getWorldSpaceFrustumCorners(cascadeProj, cascadeView) };
    auto frustumCenter { getFrustumCenterFromCorners(frustumCorners) };

    glm::vec3 min{ std::numeric_limits<float>::max() };
    glm::vec3 max{ std::numeric_limits<float>::lowest() };
    for (const auto& corner : frustumCorners)
    {
        const glm::vec3 trf{ glm::vec4{ corner, 1.0f } };
        min = glm::min(min, trf);
        max = glm::max(max, trf);
    }

    float radius{ 0.5f * glm::distance(min, max) };
    radius = std::floor(radius);
    radius = 0.5f * glm::distance(frustumCorners[3], frustumCorners[5]);
    
    float texelsPerUnit{ static_cast<float>(shadowMapLength) / (2.0f * radius) };

    glm::mat4 scalar{ glm::scale(glm::mat4{ 1.0f }, glm::vec3{ texelsPerUnit }) };
    glm::mat4 lookAt{ glm::lookAt(glm::vec3{ 0.0f }, lightDir, glm::vec3{ 0.0f, 1.0f, 0.0f })};
    lookAt = scalar * lookAt;
    //lookAt = lookAt * scalar; Not sure if order matters here
    glm::mat4 lookAtInv{ glm::inverse(lookAt) };

    // Stabilize the matrix to prevent texture shimmering
    frustumCenter = lookAt * glm::vec4{ frustumCenter, 1.0f };
    frustumCenter.x = std::floor(frustumCenter.x);
    frustumCenter.y = std::floor(frustumCenter.y);
    frustumCenter = lookAtInv * glm::vec4{ frustumCenter, 1.0f };

    glm::vec3 eye{ frustumCenter + (lightDir * (2.0f * radius)) };

    //glm::vec3 origin{ 0.0f };


    ViewProj lightViewProj{};
    lightViewProj.view = glm::lookAt(eye, frustumCenter, glm::vec3{ 0.0f, 1.0f, 0.0f });

    // Near and far planes have swapped values for reverse-Z depth buffers
    lightViewProj.proj = glm::orthoZO(-radius, radius, -radius, radius, 15.0f * radius, 15.0f * -radius);
    lightViewProj.zNear = 10.0f * radius;
    lightViewProj.zFar = 10.0f * -radius;
    lightViewProj.cascadeRadius = radius;

    return lightViewProj;

    /*
    lightViewProj.view = glm::lookAt(frustumCenter + lightDir, frustumCenter, glm::vec3{ 0.0f, 1.0f, 0.0f });
    // zNear and zFar swapped for reverse-Z depth
    // vals start at 6
    
    glm::vec3 min{ std::numeric_limits<float>::max() };
    glm::vec3 max{ std::numeric_limits<float>::min() };
    for (const auto& corner : frustumCorners)
    {
        const glm::vec3 trf{ lightViewProj.view * glm::vec4{ corner, 1.0f } };
        min = glm::min(min, trf);
        max = glm::max(max, trf);
    }

    constexpr float zMult{ 1.0f };
    //min.z = min.z < 0.0f ? min.z * zMult : min.z / zMult;
    //max.z = max.z < 0.0f ? max.z / zMult : max.z * zMult;
    // better:
    //min.z = min.z < 0.0f ? min.z / zMult : min.z * zMult;
    //max.z = max.z < 0.0f ? max.z * zMult : max.z / zMult;

    //max.z -= 50.0f;
    //min.z += 50.0f;

    lightViewProj.proj = glm::orthoZO(min.x, max.x, min.y, max.y, max.z, min.z);
    lightViewProj.zNear = max.z;
    lightViewProj.zFar = min.z;
    */
}


struct LoadTextureResults
{
    int width{};
    int height{};
    GLuint texture{};
};
LoadTextureResults loadTexture(const char* fileName, int desiredChannels, GLenum format, GLenum internalFormat, GLenum type)
{
    int width{};
    int height{};
    int channels{};
    unsigned char* pixels{ stbi_load(fileName, &width, &height, &channels, desiredChannels) };

    if (!pixels)
    {
        std::cerr << "Failure\n";
    }

    GLuint texture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, internalFormat, width, height);
    glTextureSubImage2D(texture, 0, 0, 0, width, height, format, type, pixels);

    stbi_image_free(pixels);

    return
    {
        width,
        height,
        texture,
    };
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
    sceneObject.mViewCount = 4;

    std::vector<SceneObject::ModelObjectLoadInfo> modelLoadInfos
    {
        //{.name{"bistro"}, .path{ "../../assets/Sponza/Sponza.gltf" }, .directory{ "../../assets/Sponza" } },
        {.name{"bistro"}, .path{ "../../assets/Bistro1.glb" } },
        //{ .name{"helmet"}, .path{ "../../assets/DamagedHelmet.glb" } },
        //{.name{"bistro"}, .path{ "../../assets/structure.glb" } },
        //{.name{"bistro"}, .path{ "../../assets/deccer2.glb" } },
        //{.name{"bistro"}, .path{ "../../assets/Bistro2.glb" } },
        //{ .name{"cubes"}, .path{ "../../assets/cubes.glb" } },
    };
    sceneObject.loadModels(modelLoadInfos);
    sceneObject.initGlMemory();

    sceneObject.mShaderPrograms["uber_mesh"] = { .msPath{ "../../src/shaders/uber.mesh" }, .fsPath{ "../../src/shaders/uber.frag" } };
    //sceneObject.mShaderPrograms["transparent"]      = { .vsPath{ "../../src/shaders/uber.vert" }, .fsPath{ "../../src/shaders/transparent.frag" } };
    sceneObject.mShaderPrograms["comp"]             = { .vsPath{ "../../src/shaders/comp.vert" }, .fsPath{ "../../src/shaders/comp.frag" } };
    sceneObject.mShaderPrograms["lighting"]         = { .vsPath{ "../../src/shaders/comp.vert" }, .fsPath{ "../../src/shaders/lighting.frag" } };
    sceneObject.mShaderPrograms["occluder_batch"]   = { .computePath{ "../../src/shaders/occluder_batch.comp" } };
    sceneObject.mShaderPrograms["cluster_batch"]    = { .computePath{ "../../src/shaders/cluster_batch.comp" } };
    sceneObject.mShaderPrograms["depth_downsample"] = { .computePath{ "../../src/shaders/depth_downsample.comp" }};
    sceneObject.mShaderPrograms["shadow_mesh"]      = { .msPath{ "../../src/shaders/shadow.mesh" }, .fsPath{ "../../src/shaders/shadow.frag" } };
    sceneObject.mShaderPrograms["skybox"]           = { .vsPath{ "../../src/shaders/skybox.vert" }, .fsPath{ "../../src/shaders/skybox.frag" } };
    sceneObject.mShaderPrograms["fxaa"]             = { .vsPath{ "../../src/shaders/comp.vert" },   .fsPath{ "../../src/shaders/fxaa.frag"   } };
    sceneObject.linkShaderPrograms();

    Camera camera({ 0.0f, 3.0f, 7.0f }, { 0.0f, 90.0f });

    float screenQuadVerts[] =
    {
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
    GLuint metallicRoughnessTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &metallicRoughnessTexture);
    glTextureStorage2D(metallicRoughnessTexture, 1, GL_RG16, screenWidth, screenHeight);
    GLuint depthTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &depthTexture);
    glTextureStorage2D(depthTexture, 1, GL_DEPTH_COMPONENT32F, screenWidth, screenHeight);

    glNamedFramebufferTexture(opaqueFBO, GL_COLOR_ATTACHMENT0, opaqueTexture, 0);
    glNamedFramebufferTexture(opaqueFBO, GL_COLOR_ATTACHMENT1, normalTexture, 0);
    glNamedFramebufferTexture(opaqueFBO, GL_COLOR_ATTACHMENT2, metallicRoughnessTexture, 0);
    glNamedFramebufferTexture(opaqueFBO, GL_DEPTH_ATTACHMENT, depthTexture, 0);

    GLenum drawBuffersG[]{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
    glNamedFramebufferDrawBuffers(opaqueFBO, 3, drawBuffersG);

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

    GLuint aaStagingFb{};
    glCreateFramebuffers(1, &aaStagingFb);
    GLuint aaStagingTex{};
    glCreateTextures(GL_TEXTURE_2D, 1, &aaStagingTex);
    glTextureStorage2D(aaStagingTex, 1, GL_SRGB8_ALPHA8, screenWidth, screenHeight);
    glNamedFramebufferTexture(aaStagingFb, GL_COLOR_ATTACHMENT0, aaStagingTex, 0);
    GLuint aaStagingDepthTex{};
    glCreateTextures(GL_TEXTURE_2D, 1, &aaStagingDepthTex);
    glTextureStorage2D(aaStagingDepthTex, 1, GL_DEPTH_COMPONENT32F, screenWidth, screenHeight);
    glNamedFramebufferTexture(aaStagingFb, GL_DEPTH_ATTACHMENT, aaStagingDepthTex, 0);
    GLenum soManyDrawBuffers[]{ GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT };
    glNamedFramebufferDrawBuffers(aaStagingFb, 2, drawBuffers);

    GLuint hiZTexture{};
    glCreateTextures(GL_TEXTURE_2D, 1, &hiZTexture);
    glTextureStorage2D(hiZTexture, std::floor(std::log2(std::max(screenWidth, screenHeight))) + 1, GL_R32F, screenWidth, screenHeight);
    GLuint64 hiZTexHandle{ glGetTextureHandleARB(hiZTexture) };
    glMakeTextureHandleResidentARB(hiZTexHandle);

    constexpr int shadowMapLength{ 1024 };

    GLuint shadowFBOs[3]{};
    glCreateFramebuffers(3, &shadowFBOs[0]);

    GLuint shadowNormalMap{};
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &shadowNormalMap);
    glTextureStorage3D(shadowNormalMap, 1, GL_RGBA16F, shadowMapLength, shadowMapLength, 3); // TODO: FIND BETTER FORMATS
    GLuint shadowRadiantFluxMap{};
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &shadowRadiantFluxMap);
    glTextureStorage3D(shadowRadiantFluxMap, 1, GL_RGBA16F, shadowMapLength, shadowMapLength, 3);
    GLuint shadowMap{};
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &shadowMap); // this format is fine
    glTextureStorage3D(shadowMap, 1, GL_DEPTH_COMPONENT32F, shadowMapLength, shadowMapLength, 3);
    //glTextureParameteri(shadowMap, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

    for (int i{ 0 }; i < 3; ++i)
    {
        glNamedFramebufferTextureLayer(shadowFBOs[i], GL_COLOR_ATTACHMENT0, shadowNormalMap,      0, i);
        glNamedFramebufferTextureLayer(shadowFBOs[i], GL_COLOR_ATTACHMENT1, shadowRadiantFluxMap, 0, i);
        glNamedFramebufferTextureLayer(shadowFBOs[i], GL_DEPTH_ATTACHMENT, shadowMap,             0, i);

        glNamedFramebufferDrawBuffers(shadowFBOs[i], 2, drawBuffers);
    }

    
    if (glCheckNamedFramebufferStatus(aaStagingFb, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
        
    GLuint shadowMapShadowSampler{};
    glCreateSamplers(1, &shadowMapShadowSampler);
    glSamplerParameteri(shadowMapShadowSampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    GLuint shadowMapDepthSampler{};
    glCreateSamplers(1, &shadowMapDepthSampler);

    GLuint shadowHiZs[3]{};
    glCreateTextures(GL_TEXTURE_2D, 3, &shadowHiZs[0]);
    GLuint64 shadowHiZHandles[3]{};
    for (int i{ 0 }; i < 3; ++i)
    {
        glTextureStorage2D(shadowHiZs[i], std::floor(std::log2(shadowMapLength)) + 1, GL_R32F, shadowMapLength, shadowMapLength);
        shadowHiZHandles[i] = glGetTextureHandleARB(shadowHiZs[i]);
        glMakeTextureHandleResidentARB(shadowHiZHandles[i]);
    }

    struct CsmGlslData
    {
        glm::mat4 viewProj{};
        glm::mat4 radius{}; // Packing workaround. Radius will be stored in [0][0]
    };

    GLuint lightCascadeMatrixBuffer{};
    glCreateBuffers(1, &lightCascadeMatrixBuffer);
    glNamedBufferStorage(lightCascadeMatrixBuffer, 3 * sizeof(CsmGlslData), nullptr, GL_MAP_WRITE_BIT);


    //GLuint dfgTex{ loadTexture("../../assets/ibl/dfg.png", 2, GL_RG, GL_RG16F, GL_UNSIGNED_BYTE).texture };

    const char* skyboxFileNames[]
    {
        "../../assets/cubemap/px.png",
        "../../assets/cubemap/nx.png",
        "../../assets/cubemap/py.png",
        "../../assets/cubemap/ny.png",
        "../../assets/cubemap/pz.png",
        "../../assets/cubemap/nz.png",
    };

    GLuint skybox{};
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &skybox);
    glTextureStorage2D(skybox, 1, GL_RGB16F, 1024, 1024);

    glTextureParameteri(skybox, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(skybox, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(skybox, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    for (int i{ 0 }; i < 6; ++i)
    {
        int width{};
        int height{};
        int channels{};
        unsigned char* pixels{ stbi_load(skyboxFileNames[i], &width, &height, &channels, 3) };
        glTextureSubImage3D(skybox, 0, 0, 0, i, 1024, 1024, 1, GL_RGB, GL_UNSIGNED_BYTE, pixels);

        stbi_image_free(pixels);
    }

    float skyboxVertices[]
    {   
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };

    GLuint skyboxVbo{};
    glCreateBuffers(1, &skyboxVbo);
    glNamedBufferStorage(skyboxVbo, sizeof(float) * 108, skyboxVertices, GL_NONE);

    GLuint skyboxVao{};
    glCreateVertexArrays(1, &skyboxVao);
    glVertexArrayVertexBuffer(skyboxVao, 0, skyboxVbo, 0, 3 * sizeof(float));

    glEnableVertexArrayAttrib(skyboxVao, 0);
    glVertexArrayAttribFormat(skyboxVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(skyboxVao, 0, 0);



    double lastTime{ SDL_GetTicks64() * 0.001 };

    Stats stats{};
    bool updateViewFrustum{ true };
    glm::vec3 inLightDir{ -1.0f, 10.0f, 2.0f };

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

        glm::vec3 lightDirection{ glm::normalize(inLightDir) };

        glm::mat4 view{ camera.getViewMatrix() };
        auto proj = infiniteReversePerspective(glm::radians(camera.mFov), 16.0f / 9.0f, camera.mZNear);
        auto tp{ proj * view };

        glm::mat4 nearProj{ glm::perspective(glm::radians(camera.mFov), 16.0f / 9.0f, camera.mZNear, 9.0f)  };
        glm::mat4 midProj { glm::perspective(glm::radians(camera.mFov), 16.0f / 9.0f, 20.0f,         36.0f)  };
        glm::mat4 farProj { glm::perspective(glm::radians(camera.mFov), 16.0f / 9.0f, 80.0f,         180.0f) };

        ViewProj lightCascadeMatrices[3]{};
        lightCascadeMatrices[0] = calculateLightMatricesForCascade(nearProj, view, lightDirection, shadowMapLength);
        lightCascadeMatrices[1] = calculateLightMatricesForCascade(midProj,  view, lightDirection, shadowMapLength);
        lightCascadeMatrices[2] = calculateLightMatricesForCascade(farProj,  view, lightDirection, shadowMapLength);
        
        void* map{};
        map = glMapNamedBuffer(lightCascadeMatrixBuffer, GL_WRITE_ONLY);
        {
            CsmGlslData lightViewProj[3]{};
            for (int i{ 0 }; i < 3; ++i)
            {
                lightViewProj[i].viewProj = lightCascadeMatrices[i].proj * lightCascadeMatrices[i].view;
                lightViewProj[i].radius[0][0] = lightCascadeMatrices[i].cascadeRadius;
            }

            std::memcpy(map, lightViewProj, 3 * sizeof(CsmGlslData));
        }
        glUnmapNamedBuffer(lightCascadeMatrixBuffer);

        if (updateViewFrustum)
        {
            hiZView = view;
        }

        glViewport(0, 0, screenWidth, screenHeight);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Debug");
        ImGui::Text("frametime %f ms", stats.frameTime);
        ImGui::InputText("shader program", selectedProgram, 512);
        ImGui::InputFloat3("light direction", &inLightDir[0]);
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
            std::vector<SceneObject::IndirectDraw> indirectDraws(sceneObject.mViewCount);
            for (int i{ 0 }; i < indirectDraws.size(); ++i)
            {
                indirectDraws[i].firstIndex = i * sceneObject.mIndexCount;
            }

            map = glMapNamedBuffer(sceneObject.mIndirectDrawBuffers, GL_WRITE_ONLY);
            std::memcpy(map, indirectDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectDraw));
            glUnmapNamedBuffer(sceneObject.mIndirectDrawBuffers);

            std::vector<SceneObject::IndirectMeshDraw> indirectMeshDraws(sceneObject.mViewCount);
            for (int i{ 0 }; i < indirectMeshDraws.size(); ++i)
            {
                indirectMeshDraws[i].first = i * (sceneObject.mClusterCount / sceneObject.mViewCount);
            }

            map = glMapNamedBuffer(sceneObject.mClusterIndirectDrawBuffer, GL_WRITE_ONLY);
            std::memcpy(map, indirectMeshDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectMeshDraw));
            glUnmapNamedBuffer(sceneObject.mClusterIndirectDrawBuffer);

            Camera::Frustum frustum{ camera.getViewFrustum(proj) };
            frustum = Camera::makeViewFrustum(proj * view);

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
                .hiZ{ hiZTexHandle },

                .projType{ SceneObject::View::VIEW_PERSPECTIVE },
            };

            for (int i{ 0 }; i < 3; ++i)
            {
                frustum = Camera::makeViewFrustum(lightCascadeMatrices[i].proj * lightCascadeMatrices[i].view);
                sceneObject.mViews[i + 1] =
                {
                    .view{ lightCascadeMatrices[i].view },
                    .proj{ lightCascadeMatrices[i].proj },

                    .top   { frustum.top },
                    .bottom{ frustum.bottom },
                    .right { frustum.right },
                    .left  { frustum.left },
                    .far   { frustum.far },
                    .near  { frustum.near },

                    .camPosAndZNear{ glm::vec3{ 0.0f }, lightCascadeMatrices[i].zNear },
                    .hiZ{ shadowHiZHandles[i] },

                    .projType{ SceneObject::View::VIEW_ORTHO },

                    .zFar{ lightCascadeMatrices[i].zFar },
                };
            }
            
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
            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("occluder_batch").program, "clustersPerView");
            glUniform1ui(loc, sceneObject.mClusterCount / sceneObject.mViewCount);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mClusterIndirectDrawBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mClusterBatchBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, sceneObject.mVisibilityBitmaskSsbo);

            glDispatchCompute(std::ceil(std::cbrt(sceneObject.mClusterCount) / 4.0f), 
                std::ceil(std::cbrt(sceneObject.mClusterCount) / 4.0f), std::ceil(std::cbrt(sceneObject.mClusterCount) / 4.0f));

            GLsync occluderBatchFence{ glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, GL_NONE) };

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_GREATER);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClearDepth(0.0f);

            glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO);
            glViewport(0, 0, screenWidth, screenHeight);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sceneObject.mClusterIndirectDrawBuffer);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, sceneObject.mClusterBatchBuffer);

            glUseProgram(sceneObject.mShaderPrograms.at("uber_mesh").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "clusterCount") }; // todo: does this exist???
            glUniform1ui(loc, sceneObject.mClusterCount / sceneObject.mViewCount);

            glWaitSync(occluderBatchFence, GL_NONE, GL_TIMEOUT_IGNORED);
            glDeleteSync(occluderBatchFence);

            glDrawMeshTasksIndirectNV(0);
            
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, sceneObject.mClusterBatchBuffer);

            glViewport(0, 0, shadowMapLength, shadowMapLength);

            glUseProgram(sceneObject.mShaderPrograms.at("shadow_mesh").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow_mesh").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow_mesh").program, "lightDir") };
            glUniform3fv(loc, 1, glm::value_ptr(lightDirection));

            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow_mesh").program, "transform") };


            glEnable(GL_DEPTH_CLAMP);

            for (int i{ 0 }; i < 3; ++i)
            {
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(lightCascadeMatrices[i].proj * lightCascadeMatrices[i].view));

                glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOs[i]);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                glDrawMeshTasksIndirectNV((1 + i) * sizeof(SceneObject::IndirectMeshDraw));
            }

            glDisable(GL_DEPTH_CLAMP);
            
            {
                glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

                if (updateViewFrustum)
                {
                    glCopyImageSubData(depthTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                        hiZTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                        screenWidth, screenHeight, 1);
                }

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

            for (int n{ 0 }; n < 3; ++n)
            {
                glCopyImageSubData(shadowMap, GL_TEXTURE_2D_ARRAY, 0, 0, 0, n,
                    shadowHiZs[n], GL_TEXTURE_2D, 0, 0, 0, 0,
                    shadowMapLength, shadowMapLength, 1);

                int hiZWidth{ shadowMapLength };
                int hiZHeight{ shadowMapLength };

                for (int i{ 0 }; i < std::floor(std::log2(shadowMapLength)); ++i)
                {
                    glBindImageTexture(0, shadowHiZs[n], i,     GL_FALSE, 0, GL_READ_ONLY,  GL_R32F);
                    glBindImageTexture(1, shadowHiZs[n], i + 1, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

                    hiZWidth /= 2;
                    hiZWidth = hiZWidth > 0 ? hiZWidth : 1;

                    hiZHeight /= 2;
                    hiZHeight = hiZHeight > 0 ? hiZHeight : 1;

                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

                    glDispatchCompute(std::ceil(hiZWidth / 32.0f), hiZHeight, 1);
                }
            }

            map = glMapNamedBuffer(sceneObject.mIndirectDrawBuffers, GL_WRITE_ONLY);
            std::memcpy(map, indirectDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectDraw));
            glUnmapNamedBuffer(sceneObject.mIndirectDrawBuffers);

            map = glMapNamedBuffer(sceneObject.mIndirectBlendDrawBuffers, GL_WRITE_ONLY);
            std::memcpy(map, indirectDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectDraw));
            glUnmapNamedBuffer(sceneObject.mIndirectBlendDrawBuffers);

            map = glMapNamedBuffer(sceneObject.mClusterIndirectDrawBuffer, GL_WRITE_ONLY);
            std::memcpy(map, indirectMeshDraws.data(), sceneObject.mViewCount * sizeof(SceneObject::IndirectMeshDraw));
            glUnmapNamedBuffer(sceneObject.mClusterIndirectDrawBuffer);

            glUseProgram(sceneObject.mShaderPrograms.at("cluster_batch").program);

            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("cluster_batch").program, "clusterCount");
            glUniform1ui(loc, sceneObject.mClusterCount);
            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("cluster_batch").program, "viewIndexCount");
            glUniform1ui(loc, sceneObject.mIndexCount);
            loc = glGetUniformLocation(sceneObject.mShaderPrograms.at("cluster_batch").program, "clustersPerView");
            glUniform1ui(loc, sceneObject.mClusterCount / sceneObject.mViewCount);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,  sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,  sceneObject.mClusterIndirectDrawBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2,  sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3,  sceneObject.mClusterBatchBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4,  sceneObject.mIndirectBlendDrawBuffers);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5,  sceneObject.mWriteBlendIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6,  sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8,  sceneObject.mTransformsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9,  sceneObject.mVisibilityBitmaskSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, sceneObject.mViewSsbo);
            
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

            glDispatchCompute(std::ceil(std::cbrt(sceneObject.mClusterCount) / 4.0f),
                std::ceil(std::cbrt(sceneObject.mClusterCount) / 4.0f), std::ceil(std::cbrt(sceneObject.mClusterCount) / 4.0f));

            GLsync clusterBatchFence{ glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, GL_NONE) };

            glBindFramebuffer(GL_FRAMEBUFFER, opaqueFBO);
            glViewport(0, 0, screenWidth, screenHeight);

            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sceneObject.mClusterIndirectDrawBuffer);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, sceneObject.mClusterBatchBuffer);

            glUseProgram(sceneObject.mShaderPrograms.at("uber_mesh").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("uber_mesh").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));

            glWaitSync(clusterBatchFence, GL_NONE, GL_TIMEOUT_IGNORED);
            glDeleteSync(clusterBatchFence);

            glDrawMeshTasksIndirectNV(0);

            glViewport(0, 0, shadowMapLength, shadowMapLength);

            glUseProgram(sceneObject.mShaderPrograms.at("shadow_mesh").program);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, sceneObject.mIbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, sceneObject.mClusterBatchBuffer);

            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow_mesh").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow_mesh").program, "lightDir") };
            glUniform3fv(loc, 1, glm::value_ptr(lightDirection));

            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("shadow_mesh").program, "transform") };

            glEnable(GL_DEPTH_CLAMP);

            for (int i{ 0 }; i < 3; ++i)
            {
                glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(lightCascadeMatrices[i].proj * lightCascadeMatrices[i].view));

                glBindFramebuffer(GL_FRAMEBUFFER, shadowFBOs[i]);

                glDrawMeshTasksIndirectNV((1 + i) * sizeof(SceneObject::IndirectMeshDraw));
            }

            glDisable(GL_DEPTH_CLAMP);
            
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
            /*
            glUseProgram(sceneObject.mShaderPrograms.at("transparent").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("transparent").program, "transform") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(tp));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("transparent").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("transparent").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));
            */
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sceneObject.mClustersSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, sceneObject.mMaterialsSsbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, sceneObject.mVbo);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneObject.mTransformsSsbo);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sceneObject.mWriteBlendIbo);
            glBindVertexArray(sceneObject.mBlendVao);
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, sceneObject.mIndirectBlendDrawBuffers);

            //glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);

            glDepthFunc(GL_ALWAYS);
            glDisable(GL_BLEND);

            //glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, aaStagingFb);
            //glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glUseProgram(sceneObject.mShaderPrograms.at("lighting").program);

            glBindTextureUnit(0, opaqueTexture);
            glBindTextureUnit(1, normalTexture);
            glBindTextureUnit(2, depthTexture);

            glBindTextureUnit(4, shadowMap);
            glBindSampler(4, shadowMapShadowSampler);

            glBindTextureUnit(5, shadowMap);
            glBindSampler(5, shadowMapDepthSampler);

            glBindTextureUnit(6, metallicRoughnessTexture);
            glBindTextureUnit(7, shadowNormalMap);
            glBindTextureUnit(8, shadowRadiantFluxMap);

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, lightCascadeMatrixBuffer);

            //loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "nearZ") };
            //glUniform1f(loc, lightCascadeMatrices[hiZDisplayLevel].zNear);
            //loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "farZ") };
            //glUniform1i(loc, lightCascadeMatrices[hiZDisplayLevel].zFar);

            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "shadowInvViewProj") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(glm::inverse(lightCascadeMatrices[0].proj * lightCascadeMatrices[0].view)));

            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "invViewProj") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(glm::inverse(tp)));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "camPos") };
            glUniform3fv(loc, 1, glm::value_ptr(camera.mPos));
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("lighting").program, "lightDir") };
            glUniform3fv(loc, 1, glm::value_ptr(lightDirection));

            glBindVertexArray(screenQuadVAO);

            glDepthMask(GL_TRUE);
            //glDisable(GL_DEPTH_TEST);
            glEnable(GL_FRAMEBUFFER_SRGB);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_FRAMEBUFFER_SRGB);
            
            glUseProgram(sceneObject.mShaderPrograms.at("skybox").program);
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("skybox").program, "proj") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(proj));
            glm::mat4 viewRot{ glm::mat3{ view } };
            loc = { glGetUniformLocation(sceneObject.mShaderPrograms.at("skybox").program, "view") };
            glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(viewRot));
            glBindTextureUnit(0, skybox);

            //glDisable(GL_DEPTH_TEST);

            glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_GEQUAL);
            glDepthMask(GL_TRUE);
            glBindVertexArray(skyboxVao);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            //glEnable(GL_DEPTH_TEST);

            glBindVertexArray(screenQuadVAO);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glUseProgram(sceneObject.mShaderPrograms.at("comp").program);

            glBindTextureUnit(0, accumTexture);
            glBindTextureUnit(1, revealTexture);

            //glDrawArrays(GL_TRIANGLES, 0, 6);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);

            glUseProgram(sceneObject.mShaderPrograms.at("fxaa").program);
            glBindTextureUnit(0, aaStagingTex);

            glDisable(GL_DEPTH_TEST);

            glEnable(GL_FRAMEBUFFER_SRGB);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisable(GL_FRAMEBUFFER_SRGB);

            //glBlitNamedFramebuffer(aaStagingFb, 0, 0, 0, screenWidth, screenHeight, 0, 0, screenWidth, screenHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

            //glBindImageTexture(0, opaqye);
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
    glDeleteFramebuffers(3, &shadowFBOs[0]);
    glDeleteFramebuffers(1, &aaStagingFb);
    
    glDeleteSamplers(1, &shadowMapDepthSampler);
    glDeleteSamplers(1, &shadowMapShadowSampler);

    glDeleteTextures(1, &aaStagingDepthTex);
    glDeleteTextures(1, &aaStagingTex);
    glDeleteTextures(1, &skybox);
    glDeleteTextures(1, &shadowRadiantFluxMap);
    glDeleteTextures(1, &shadowNormalMap);
    glDeleteTextures(1, &opaqueTexture);
    glDeleteTextures(1, &accumTexture);
    glDeleteTextures(1, &revealTexture);
    glDeleteTextures(1, &normalTexture);
    glDeleteTextures(1, &metallicRoughnessTexture);
    glDeleteTextures(1, &depthTexture);
    glDeleteTextures(1, &hiZTexture);
    glDeleteTextures(1, &shadowMap);
    glDeleteTextures(3, &shadowHiZs[0]);


    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();

    return 0;
}