#pragma once

#include "glad/glad.h"
#include "glm/glm.hpp"

#include "fastgltf/core.hpp"

#include <cstddef> // for std::size_t
#include <cstdint>
#include <filesystem>
#include <vector>

class ModelObject final
{
public:

	struct Vertex
	{
		glm::vec3 pos{};
		glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
		glm::vec2 uv{};
	};

	struct Texture
	{
		int sampler{};
		int image{};

		GLuint64 bindlessHandle{};
	};

	enum AlphaMode
	{
		OPAQUE = 0b0001,
		MASK   = 0b0010,
		BLEND  = 0b0100,
	};

	struct Material
	{
		glm::vec4 colorFactor{};
		float metallicFactor{};
		float roughnessFactor{};

		int colorTexture{ -1 };
		int metallicRoughnessTexture{ -1 };
		int normalTexture{ -1 };

		bool doubleSided{ false };
		AlphaMode alphaMode{};

		// todo: remove
		GLuint ubo{};
	};
	struct MaterialUbo
	{
		glm::vec4 colorFactor{};
		GLfloat metallicFactor{};
		GLfloat roughnessFactor{};

		GLint hasColorTexture{ false };
		GLuint64 colorTexture{};

		GLint hasMetallicRoughnessTexture{ false };
		GLuint64 metallicRoughnessTexture{};

		GLint hasNormalTexture{ false };
		GLuint64 normalTexture{};

		GLint alphaMask{};
		GLfloat alphaCutoff{};
	};

	struct DrawIndirect
	{
		GLuint count{};
		GLuint instanceCount{};
		GLuint firstIndex{};
		GLint baseVertex{};
		GLuint baseInstance{};
	};

	struct Meshlet
	{
		GLsizei indexOffset{};
		GLsizei indexCount{};

		GLuint drawIndirectBuffer{};

		int triangleCount{};
	};

	struct Primitive
	{
		int material{ -1 };

		std::vector<Meshlet> meshlets{};
	};

	struct Mesh
	{
		std::vector<Primitive> primitives{};
	};

	struct Node
	{
		std::vector<int> children{};
		int mesh{ -1 };

		glm::mat4 localTransform{};
	};

	ModelObject(const std::filesystem::path& path, int vertexOffset, int indexOffset, const std::filesystem::path& directory = "assets");

	ModelObject(const ModelObject&) = delete;
	ModelObject& operator=(const ModelObject&) = delete;

	~ModelObject();

	void draw(const glm::mat4& transform, GLuint shaderProgram, AlphaMode alphaMode, int materialOffset, 
		int& triangles, int& draws);
	void renderNodeAndChildren(const Node& node, const glm::mat4& parentTransform, GLuint shaderProgram,
		AlphaMode alphaMode, int materialOffset, int& triangles, int& draws);

	std::vector<Node> mNodes{};
	std::vector<int> mRootNodes{};

	std::vector<Mesh> mMeshes{};

	std::vector<GLuint> mSamplers{};
	GLuint mDefaultSampler{};
	std::vector<GLuint> mImages{};
	std::vector<Texture> mTextures{};
	
	// Not yet bindless
	std::vector<Material> mMaterials{};
	std::vector<MaterialUbo> mMaterialBuffers{};

	std::vector<Vertex> mVertices{};
	std::vector<std::uint32_t> mIndices{};

	// Not yet bindless
	//GLuint mVbo{};
	//GLuint mIbo{};
	//GLuint mVao{};

private:
	
	void loadNodes(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadMeshes(fastgltf::Expected<fastgltf::Asset>& asset, int vertexOffset, int globalIndexOffset);
	void loadSamplers(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadImages(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadTextures(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadMaterials(const fastgltf::Expected<fastgltf::Asset>& asset);

	void uploadGeometry();

};