#include "scene.hpp"

#include "../camera/camera.hpp"
#include "../model/model.hpp"

#include "glad/glad.h"
#include "glm/glm.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

SceneObject::~SceneObject()
{
	glDeleteBuffers(1, &mMaterialsSsbo);
	glDeleteBuffers(1, &mTransformsSsbo);
	glDeleteBuffers(1, &mClustersSsbo);

	glDeleteBuffers(1, &mVbo);
	glDeleteBuffers(1, &mIbo);

	glDeleteVertexArrays(1, &mVao);
	glDeleteVertexArrays(1, &mBlendVao);

	glDeleteBuffers(1, &mWriteIbo);
	glDeleteBuffers(1, &mIndirectDrawBuffer);

	glDeleteBuffers(1, &mWriteBlendIbo);
	glDeleteBuffers(1, &mIndirectBlendDrawBuffer);

	for (auto& [name, shaderProgram] : mShaderPrograms)
	{
		glDeleteProgram(shaderProgram.program);
	}
}

void SceneObject::loadModels(const std::vector<ModelObjectLoadInfo>& loadInfo)
{
	for (const auto& info : loadInfo)
	{
		mModels[info.name] = ModelObject{ info.path, mVertexCount, mIndexCount, mMaterialCount, mTransformCount, info.directory };

		mMaterialCount += mModels[info.name].mMaterials.size();
		mTransformCount += mModels[info.name].mGlobalTransforms.size();
		mClusterCount += mModels[info.name].mClusters.size();
		mVertexCount += mModels[info.name].mVertices.size();
		mIndexCount += mModels[info.name].mIndices.size();
		mBlendIndexCount += mModels[info.name].mBlendIndexCount;
	}
}

void SceneObject::initGlMemory()
{
	glCreateBuffers(1, &mMaterialsSsbo);
	glNamedBufferStorage(mMaterialsSsbo, mMaterialCount * sizeof(ModelObject::Material), nullptr, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &mTransformsSsbo);
	glNamedBufferData(mTransformsSsbo, sizeof(glm::mat4) * mTransformCount, nullptr, GL_STATIC_DRAW);

	glCreateBuffers(1, &mClustersSsbo);
	glNamedBufferStorage(mClustersSsbo, sizeof(ModelObject::Cluster) * mClusterCount, nullptr, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &mVbo);
	glNamedBufferStorage(mVbo, sizeof(ModelObject::Vertex) * mVertexCount, nullptr, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &mIbo);
	glNamedBufferStorage(mIbo, mIndexCount * sizeof(std::uint32_t), nullptr, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &mViewFrustumSsbo);
	glNamedBufferStorage(mViewFrustumSsbo, sizeof(Camera::Frustum), nullptr, GL_MAP_WRITE_BIT);

	GLsizei visibilityBitmaskSize{ static_cast<GLsizei>(std::ceil(mClusterCount / 8.0f)) };
	visibilityBitmaskSize = ((visibilityBitmaskSize + 32 - 1) / 32) * 32; // Rounded up to a multiple of 32 because OpenGL GLSL only supports 32 bit types

	glCreateBuffers(1, &mVisibilityBitmaskSsbo);
	glNamedBufferStorage(mVisibilityBitmaskSsbo, visibilityBitmaskSize, nullptr, GL_NONE);
	GLubyte visibilityClearData{ 0 };
	glClearNamedBufferData(mVisibilityBitmaskSsbo, GL_R8UI, GL_RED, GL_UNSIGNED_BYTE, &visibilityClearData);

	int materialOffset { 0 };
	int transformOffset{ 0 };
	int clusterOffset  { 0 };
	int vertexOffset   { 0 };
	int indexOffset    { 0 };

	for (const auto& [name, model] : mModels)
	{
		glNamedBufferSubData(mMaterialsSsbo, materialOffset * sizeof(ModelObject::Material),
			model.mMaterials.size() * sizeof(ModelObject::Material), model.mMaterials.data());

		glNamedBufferSubData(mTransformsSsbo, transformOffset * sizeof(glm::mat4),
			model.mGlobalTransforms.size() * sizeof(glm::mat4), model.mGlobalTransforms.data());

		glNamedBufferSubData(mClustersSsbo, clusterOffset * sizeof(ModelObject::Cluster),
			model.mClusters.size() * sizeof(ModelObject::Cluster), model.mClusters.data());

		glNamedBufferSubData(mVbo, vertexOffset * sizeof(ModelObject::Vertex),
			model.mVertices.size() * sizeof(ModelObject::Vertex), model.mVertices.data());

		glNamedBufferSubData(mIbo, indexOffset * sizeof(std::uint32_t),
			model.mIndices.size() * sizeof(std::uint32_t), model.mIndices.data());

		materialOffset += model.mMaterials.size();
		transformOffset += model.mGlobalTransforms.size();
		clusterOffset += model.mClusters.size();
		vertexOffset += model.mVertices.size();
		indexOffset += model.mIndices.size();
	}

	glCreateVertexArrays(1, &mVao);
	glCreateVertexArrays(1, &mBlendVao);

	glCreateBuffers(1, &mWriteIbo);
	glNamedBufferStorage(mWriteIbo, mIndexCount * sizeof(GLuint), nullptr, GL_NONE);

	IndirectDraw indirectDraw{};
	glCreateBuffers(1, &mIndirectDrawBuffer);
	glNamedBufferStorage(mIndirectDrawBuffer, sizeof(IndirectDraw), &indirectDraw, GL_MAP_WRITE_BIT);

	glVertexArrayElementBuffer(mVao, mWriteIbo);

	glCreateBuffers(1, &mWriteBlendIbo);
	glNamedBufferStorage(mWriteBlendIbo, mBlendIndexCount * sizeof(GLuint), nullptr, GL_NONE);

	glCreateBuffers(1, &mIndirectBlendDrawBuffer);
	glNamedBufferStorage(mIndirectBlendDrawBuffer, sizeof(IndirectDraw), &indirectDraw, GL_MAP_WRITE_BIT);

	glVertexArrayElementBuffer(mBlendVao, mWriteBlendIbo);
}

void SceneObject::linkShaderPrograms()
{
	for (auto& [name, shaderProgram] : mShaderPrograms)
	{
		linkShaderProgram(shaderProgram);
	}
}

void SceneObject::linkShaderProgram(ShaderProgram& shaderProgram)
{
	shaderProgram.program = glCreateProgram();

	if (!shaderProgram.vsPath.empty())
	{
		auto vertexShader{ compileShader(shaderProgram.vsPath, GL_VERTEX_SHADER) };
		glAttachShader(shaderProgram.program, vertexShader);
		glDeleteShader(vertexShader);
	}
	if (!shaderProgram.fsPath.empty())
	{
		auto fragmentShader{ compileShader(shaderProgram.fsPath, GL_FRAGMENT_SHADER) };
		glAttachShader(shaderProgram.program, fragmentShader);
		glDeleteShader(fragmentShader);
	}
	if (!shaderProgram.computePath.empty())
	{
		auto computeShader{ compileShader(shaderProgram.computePath, GL_COMPUTE_SHADER) };
		glAttachShader(shaderProgram.program, computeShader);
		glDeleteShader(computeShader);
	}

	glLinkProgram(shaderProgram.program);

	GLint success{};
	glGetProgramiv(shaderProgram.program, GL_LINK_STATUS, &success);
	if (!success) {
		GLchar infoLog[512]{};
		glGetProgramInfoLog(shaderProgram.program, 512, nullptr, infoLog);
		std::cerr << infoLog << '\n';
	}
}

GLuint SceneObject::compileShader(const std::string& filename, GLenum type)
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