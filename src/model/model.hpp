#pragma once

#include "glad/glad.h"
#include "glm/glm.hpp"

#include "fastgltf/core.hpp"

#include <cstddef> // for std::size_t
#include <cstdint>
#include <filesystem>
#include <vector>

// ModelObject is not guaranteed to contain any data
class ModelObject final
{
public:

	struct Vertex
	{
		glm::vec3 pos{};
		float u{};
		glm::vec3 normal{ 0.0f, 0.0f, 1.0f };
		float v{};
	};

	struct Texture
	{
		int sampler{};
		int image{};

		GLuint64 bindlessHandle{};
	};

	struct Material
	{
		glm::vec4 colorFactor{};

		GLuint64 colorTexture{};
		GLuint64 metallicRoughnessTexture{};
		GLuint64 normalTexture{};

		GLfloat metallicFactor{};
		GLfloat roughnessFactor{};

		GLint hasColorTexture{ false };
		GLint hasMetallicRoughnessTexture{ false };
		GLint hasNormalTexture{ false };

		GLint alphaMask{};
		GLfloat alphaCutoff{};
		GLint alphaBlend{};

		GLint64 padding{};
	};

	struct Meshlet
	{
		glm::vec4 boundingSphere{};

		GLint triangleCount{};
		GLuint firstIndex{};
		GLint sceneVertexOffset{};
	};

	// Clusters are instances of meshlets.
	struct Cluster
	{
		glm::vec4 boundingSphere{};

		GLuint transformIndex{};
		GLint materialIndex{ -1 };

		GLuint indexCount{};
		GLuint firstIndex{};
		GLint vertexOffset{};

		GLuint viewId{};

		GLint padding1{};
		GLint padding2{};
	};

	struct Primitive
	{
		int sceneMaterialIndex{ -1 };
		int localMaterialIndex{ -1 };

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

	// No operations should expect/require the ModelObject to contain data
	ModelObject() = default;

	ModelObject(const std::filesystem::path& path, int sceneVertexOffset, int sceneIndexOffset, 
		int sceneMaterialOffset, int sceneTransformOffset, const std::filesystem::path& directory = "assets");

	ModelObject(const ModelObject&) = delete;
	ModelObject& operator=(const ModelObject&) = delete;

	ModelObject(ModelObject&& o);
	ModelObject& operator=(ModelObject&& o);

	~ModelObject();

	void buildPrimitiveUniforms(int sceneMaterialOffset, int sceneTransformOffset);
	void buildPrimitiveUniformsFromNodeAndChildren(const Node& node, const glm::mat4& parentTransform, 
		int sceneMaterialOffset, int sceneTransformOffset);

	std::vector<Node> mNodes{};
	std::vector<int> mRootNodes{};

	std::vector<Mesh> mMeshes{};
	int mPrimitiveCount{};

	std::vector<Cluster> mClusters{};

	std::vector<GLuint> mSamplers{};
	GLuint mDefaultSampler{};
	std::vector<GLuint> mImages{};
	std::vector<Texture> mTextures{};
	
	std::vector<glm::mat4> mGlobalTransforms{};
	std::vector<Material> mMaterials{};

	std::vector<Vertex> mVertices{};
	std::vector<std::uint32_t> mIndices{};
	int mBlendIndexCount{};

private:
	
	void loadNodes(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadMeshes(fastgltf::Expected<fastgltf::Asset>& asset, GLint sceneVertexOffset, GLuint sceneIndexOffset, int sceneMaterialOffset);
	void loadSamplers(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadImages(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadTextures(const fastgltf::Expected<fastgltf::Asset>& asset);
	void loadMaterials(const fastgltf::Expected<fastgltf::Asset>& asset);

	void moveFrom(ModelObject&& o);
	void cleanup();
};