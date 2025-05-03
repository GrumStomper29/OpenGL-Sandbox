#pragma once

#include "../camera/camera.hpp"
#include "../model/model.hpp"

#include "glad/glad.h"

#include <filesystem>
#include <string>
#include <unordered_map>

class SceneObject
{
public:

	struct ModelObjectLoadInfo
	{
		std::string name{};
		std::filesystem::path path{};
		std::filesystem::path directory{ "../../assets" };
	};

	struct ShaderProgram
	{
		std::string vsPath{};
		std::string fsPath{};
		std::string computePath{};

		GLuint program{};
	};

	struct IndirectDraw
	{
		GLuint count{ 0 };
		GLuint instanceCount{ 1 };
		GLuint firstIndex{ 0 };
		GLint  baseVertex{ 0 };
		GLuint baseInstance{ 0 };
	};

	struct View
	{
		glm::mat4 view{ 1.0f };
		glm::mat4 proj{ 1.0f };

		glm::vec4 top{};
		glm::vec4 bottom{};

		glm::vec4 right{};
		glm::vec4 left{};

		glm::vec4 far{};
		glm::vec4 near{};

		glm::vec4 camPosAndZNear{};
		GLuint64 hiZ{};

		int padding0{};
		int padding1{};
	};

	SceneObject() = default;

	SceneObject(const SceneObject&) = delete;
	SceneObject& operator=(const SceneObject&) = delete;

	~SceneObject();

	void loadModels(const std::vector<ModelObjectLoadInfo>& loadInfo);
	void initGlMemory();

	void linkShaderPrograms();
	static void linkShaderProgram(ShaderProgram& shaderProgram);
	static GLuint compileShader(const std::string& filename, GLenum type);

	std::unordered_map<std::string, ModelObject> mModels{};

	int mViewCount{ 1 };
	std::vector<View> mViews{};

	// Per primitive
	GLuint mTransformsSsbo{};

	GLuint mMaterialsSsbo{};

	// All instances of all meshlets in the scene. Includes their transforms and materials
	GLuint mClustersSsbo{};

	GLuint mVbo{};
	GLuint mIbo{};

	GLuint mVao{};
	GLuint mWriteIbo{}; // Encodes cluster ID in each index for material/transform access

	GLuint mBlendVao{};
	GLuint mWriteBlendIbo{};

	GLuint mIndirectDrawBuffers{};
	GLuint mIndirectBlendDrawBuffers{};

	GLuint mVisibilityBitmaskSsbo{};

	GLuint mViewSsbo{};

	GLsizei mMaterialCount{ 0 };
	GLsizei mTransformCount{ 0 };
	GLsizei mClusterCount{ 0 };
	GLsizei mVertexCount{ 0 };
	GLsizei mIndexCount{ 0 };
	GLsizei mBlendIndexCount{ 0 };

	std::unordered_map<std::string, ShaderProgram> mShaderPrograms{};

private:

};