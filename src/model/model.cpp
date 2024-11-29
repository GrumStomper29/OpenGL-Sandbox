#include "model.hpp"

#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
#include "fastgltf/tools.hpp"

#include "fastgltf/glm_element_traits.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include "glad/glad.h"

#include "meshoptimizer/meshoptimizer.h"

#include <algorithm> // for transform & max
#include <cmath>
#include <cstddef> // for size_t
#include <cstdint>
#include <execution> // for std::execution::par
#include <filesystem>
#include <iostream>
#include <random> // for mt19937
#include <unordered_set>
#include <utility> // for move()
#include <variant>
#include <vector>



fastgltf::math::fmat4x4 calculateTrsMatrix(const fastgltf::TRS& trs)
{
	auto t{ fastgltf::math::translate(fastgltf::math::fmat4x4{ 1.0f }, trs.translation) };
	auto r{ fastgltf::math::rotate(fastgltf::math::fmat4x4{ 1.0f }, trs.rotation) };
	auto s{ fastgltf::math::scale(fastgltf::math::fmat4x4{ 1.0f }, trs.scale) };
	return t * r * s;
}

glm::mat4 toGlmMat4(const fastgltf::math::fmat4x4& m)
{
	glm::mat4 newMat4{};
	for (int i{ 0 }; i < 4; ++i)
	{
		for (int n{ 0 }; n < 4; ++n)
		{
			newMat4[i][n] = m[i][n];
		}
	}

	return newMat4;
}



ModelObject::ModelObject(const std::filesystem::path& path, int sceneVertexOffset, 
	int sceneIndexOffset, int sceneMaterialOffset, int sceneTransformOffset, const std::filesystem::path& directory)
{
	fastgltf::Parser parser{};

	auto data{ fastgltf::GltfDataBuffer::FromPath(path) };
	if (auto error{ data.error() }; error != fastgltf::Error::None)
	{
		std::cerr << "Failed to load GLTF file. Error code: "
			<< static_cast<int>(error) << '\n';
	}

	auto asset{ parser.loadGltf(data.get(), directory,
		fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages) };
	if (auto error{ asset.error() }; error != fastgltf::Error::None)
	{
		std::cerr << "Failed to load GLTF file. Error code: "
			<< static_cast<int>(error) << '\n';
	}

	if (auto error{ fastgltf::validate(asset.get()) }; error != fastgltf::Error::None)
	{
		std::cerr << "Failed to validate GLTF file. Error code: "
			<< static_cast<int>(error) << '\n';
	}

	if (asset->scenes.size() > 1)
	{
		std::cerr << "Warning: GLTF file contains multiple scenes. All but the first will be ignored.\n";
	}

	loadNodes(asset);

	mRootNodes.resize(asset->scenes[0].nodeIndices.size());
	for (int i{ 0 }; i < asset->scenes[0].nodeIndices.size(); ++i)
	{
		mRootNodes[i] = asset->scenes[0].nodeIndices[i];
	}

	loadMeshes(asset, sceneVertexOffset, sceneIndexOffset, sceneMaterialOffset);

	loadSamplers(asset);
	loadImages(asset);

	loadTextures(asset);

	loadMaterials(asset);

	buildPrimitiveUniforms(sceneMaterialOffset, sceneTransformOffset);
}

ModelObject::ModelObject(ModelObject&& o)
{
	moveFrom(std::move(o));
}

ModelObject& ModelObject::operator=(ModelObject&& o)
{

	cleanup();
	moveFrom(std::move(o));

	return *this;
}

ModelObject::~ModelObject()
{
	cleanup();
}



void ModelObject::buildPrimitiveUniforms(int sceneMaterialOffset, int sceneTransformOffset)
{
	mGlobalTransforms.clear();
	mGlobalTransforms.reserve(mPrimitiveCount);

	for (auto rootNodeIndex : mRootNodes)
	{
		buildPrimitiveUniformsFromNodeAndChildren(mNodes[rootNodeIndex], glm::mat4{ 1.0f }, sceneMaterialOffset, sceneTransformOffset);
	}
}

void ModelObject::buildPrimitiveUniformsFromNodeAndChildren(const Node& node, const glm::mat4& parentTransform, 
	int sceneMaterialOffset, int sceneTransformOffset)
{
	if (node.mesh != -1)
	{
		for (const auto& primitive : mMeshes[node.mesh].primitives)
		{
			glm::mat4 globalTransform{ parentTransform * node.localTransform };
			mGlobalTransforms.push_back(std::move(globalTransform));

			for (const auto& cluster : primitive.meshlets)
			{
				Cluster newCluster{};

				newCluster.transformIndex = (mGlobalTransforms.size() - 1) + sceneTransformOffset;
				newCluster.materialIndex = primitive.sceneMaterialIndex;
				
				newCluster.indexCount = cluster.triangleCount * 3;
				newCluster.firstIndex = cluster.firstIndex;
				newCluster.vertexOffset = cluster.sceneVertexOffset;

				mClusters.push_back(std::move(newCluster));
			}
		}
	}

	for (auto childIndex : node.children)
	{
		buildPrimitiveUniformsFromNodeAndChildren(mNodes[childIndex], parentTransform * node.localTransform, sceneMaterialOffset, sceneTransformOffset);
	}
}



void ModelObject::loadNodes(const fastgltf::Expected<fastgltf::Asset>& asset)
{
	mNodes.resize(asset->nodes.size());
	for (int i{ 0 }; i < asset->nodes.size(); ++i)
	{
		mNodes[i].children.resize(asset->nodes[i].children.size());
		for (int n{ 0 }; n < asset->nodes[i].children.size(); ++n)
		{
			mNodes[i].children[n] = asset->nodes[i].children[n];
		}

		mNodes[i].mesh = asset->nodes[i].meshIndex.value_or(-1);

		fastgltf::math::fmat4x4 localTransform
		{
			std::holds_alternative<fastgltf::TRS>(asset->nodes[i].transform)
			? calculateTrsMatrix(std::get<fastgltf::TRS>(asset->nodes[i].transform))
			: std::get<fastgltf::math::fmat4x4>(asset->nodes[i].transform)
		};
		mNodes[i].localTransform = toGlmMat4(localTransform);
	}
}

void ModelObject::loadMeshes(fastgltf::Expected<fastgltf::Asset>& asset, GLint sceneVertexOffset, GLuint sceneIndexOffset, int sceneMaterialOffset)
{
	mMeshes.resize(asset->meshes.size());
	for (int i{ 0 }; i < asset->meshes.size(); ++i)
	{
		mMeshes[i].primitives.resize(asset->meshes[i].primitives.size());

		for (int n{ 0 }; n < asset->meshes[i].primitives.size(); ++n)
		{
			Primitive& newPrimitive{ mMeshes[i].primitives[n] };

			// I can't immediately put the primitive into the mVertices & mIndices arrays because
			// its vertices/indices must be reordered and are typically changed in size
			std::vector<Vertex> primitiveVertices{};
			std::vector<GLuint> primitiveIndices{};

			GLint localVertexOffset{ static_cast<GLint>(mVertices.size()) };
			GLuint localIndexOffset{ static_cast<GLuint>(mIndices.size()) };

			{
				auto accessor{ asset->accessors[asset->meshes[i].primitives[n].indicesAccessor.value()] };

				primitiveIndices.resize(accessor.count);

				fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset.get(), accessor, [&](std::uint32_t val, std::size_t k) {
					primitiveIndices[k] = val;
					});
			}

			{
				auto accessor{ asset->accessors[asset->meshes[i].primitives[n].findAttribute("POSITION")->second] };

				primitiveVertices.resize(accessor.count);

				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), accessor, [&](glm::vec3 v, std::size_t k) {
					primitiveVertices[k].pos = v;
					});
			}

			{
				auto normals{ asset->meshes[i].primitives[n].findAttribute("NORMAL") };

				if (normals != asset->meshes[i].primitives[n].attributes.end())
				{
					auto accessor{ asset->accessors[normals->second] };

					fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), accessor, [&](glm::vec3 v, std::size_t k) {
						primitiveVertices[k].normal = v;
						});

				}
				else
				{
					std::cerr << "Warning: normals not found for mesh.\n";
				}
			}

			{
				auto uvs{ asset->meshes[i].primitives[n].findAttribute("TEXCOORD_0") };

				if (uvs != asset->meshes[i].primitives[n].attributes.end())
				{
					auto accessor{ asset->accessors[uvs->second] };

					fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), accessor, [&](glm::vec2 v, std::size_t k) {
						primitiveVertices[k].u = v.x;
						primitiveVertices[k].v = v.y;
						});
				}
			}

			std::size_t maxMeshlets{ meshopt_buildMeshletsBound(primitiveIndices.size(), 64, 124) };
			std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
			std::vector<unsigned int> meshletVertices(maxMeshlets * 64);
			std::vector<unsigned char> meshletTriangles(maxMeshlets * 124 * 3);

			std::size_t meshletCount{ meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(),
				meshletTriangles.data(), primitiveIndices.data(), primitiveIndices.size(), &primitiveVertices[0].pos.x, primitiveVertices.size(),
				sizeof(ModelObject::Vertex), 64, 124, 0.0f) };

			const meshopt_Meshlet& last{ meshlets[meshletCount - 1] };
			meshletVertices.resize(last.vertex_offset + last.vertex_count);
			meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
			meshlets.resize(meshletCount);

			mIndices.resize(mIndices.size() + meshletTriangles.size());
			for (int i{ 0 }; i < meshletTriangles.size(); ++i)
			{
				mIndices[localIndexOffset + i] = static_cast<GLuint>(meshletTriangles[i]);
			}
			auto materialIndex{ asset->meshes[i].primitives[n].materialIndex.value_or(-1) };
			if (materialIndex != -1)
			{
				if (asset->materials[materialIndex].alphaMode == fastgltf::AlphaMode::Blend)
				{
					mBlendIndexCount += meshletTriangles.size();
				}
			}

			mVertices.resize(mVertices.size() + meshletVertices.size());
			for (int i{ 0 }; i < meshletVertices.size(); ++i)
			{
				mVertices[localVertexOffset + i] = primitiveVertices[meshletVertices[i]];
			}

			newPrimitive.meshlets.resize(meshlets.size());
			for (int i{ 0 }; i < meshlets.size(); ++i)
			{
				newPrimitive.meshlets[i].triangleCount = meshlets[i].triangle_count;
				newPrimitive.meshlets[i].firstIndex = localIndexOffset + meshlets[i].triangle_offset
					+ sceneIndexOffset;
				newPrimitive.meshlets[i].sceneVertexOffset = static_cast<GLint>(meshlets[i].vertex_offset) + sceneVertexOffset
					+ localVertexOffset;
			}

			newPrimitive.localMaterialIndex = asset->meshes[i].primitives[n].materialIndex.value_or(-1);
			newPrimitive.sceneMaterialIndex = newPrimitive.localMaterialIndex == -1 ? -1 
				: newPrimitive.localMaterialIndex + sceneMaterialOffset;

			++mPrimitiveCount;
		}
	}
}

void ModelObject::loadSamplers(const fastgltf::Expected<fastgltf::Asset>& asset)
{
	mSamplers.resize(asset->samplers.size());
	for (int i{ 0 }; i < asset->samplers.size(); ++i)
	{
		GLenum wrapS{};
		switch (asset->samplers[i].wrapS)
		{
		case fastgltf::Wrap::Repeat: wrapS = GL_REPEAT; break;
		case fastgltf::Wrap::MirroredRepeat: wrapS = GL_MIRRORED_REPEAT; break;
		case fastgltf::Wrap::ClampToEdge: wrapS = GL_CLAMP_TO_EDGE; break;
		default: wrapS = GL_REPEAT; break;
		}

		GLenum wrapT{};
		switch (asset->samplers[i].wrapT)
		{
		case fastgltf::Wrap::Repeat: wrapT = GL_REPEAT; break;
		case fastgltf::Wrap::MirroredRepeat: wrapT = GL_MIRRORED_REPEAT; break;
		case fastgltf::Wrap::ClampToEdge: wrapT = GL_CLAMP_TO_EDGE; break;
		default: wrapT = GL_REPEAT; break;
		}

		glCreateSamplers(1, &mSamplers[i]);
		glSamplerParameteri(mSamplers[i], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(mSamplers[i], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glSamplerParameterf(mSamplers[i], GL_TEXTURE_MAX_ANISOTROPY, 4.0f);
		glSamplerParameteri(mSamplers[i], GL_TEXTURE_WRAP_S, wrapS);
		glSamplerParameteri(mSamplers[i], GL_TEXTURE_WRAP_T, wrapT);
	}

	glCreateSamplers(1, &mDefaultSampler);
	glSamplerParameteri(mDefaultSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(mDefaultSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glSamplerParameterf(mDefaultSampler, GL_TEXTURE_MAX_ANISOTROPY, 4.0f);
	glSamplerParameteri(mDefaultSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glSamplerParameteri(mDefaultSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void ModelObject::loadImages(const fastgltf::Expected<fastgltf::Asset>& asset)
{
	struct ImageData
	{
		stbi_uc* data{};
		int width{};
		int height{};
		int channels{};
	};

	std::vector<ImageData> datas(asset->images.size());

	std::transform(std::execution::par, asset->images.cbegin(), asset->images.cend(), datas.begin(),
		[&](const fastgltf::Image& image) -> ImageData {

			ImageData data{};

			if (std::holds_alternative<fastgltf::sources::BufferView>(image.data))
			{
				const auto& bufferView{ asset->bufferViews[
					std::get<fastgltf::sources::BufferView>(image.data).bufferViewIndex] };
				const auto& buffer{ asset->buffers[bufferView.bufferIndex] };

				if (std::holds_alternative<fastgltf::sources::Array>(buffer.data))
				{
					const auto& array{ std::get<fastgltf::sources::Array>(buffer.data) };
					data.data = stbi_load_from_memory(array.bytes.data() + bufferView.byteOffset, bufferView.byteLength,
						&data.width, &data.height, &data.channels, 4);
				}
				else
				{
					std::cerr << "Warning: unrecognized image bufferView data source.";
				}
			}
			else if (std::holds_alternative<fastgltf::sources::Array>(image.data))
			{
				const auto& array{ std::get<fastgltf::sources::Array>(image.data) };
				data.data = stbi_load_from_memory(array.bytes.data(), array.bytes.size(),
					&data.width, &data.height, &data.channels, 4);
			}
			else
			{
				std::cerr << "Warning: unrecognized image data source.";
			}

			return data;
		});

	mImages.resize(asset->images.size());
	for (int i{ 0 }; i < asset->images.size(); ++i)
	{
		// GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
		glCreateTextures(GL_TEXTURE_2D, 1, &mImages[i]);
		glTextureStorage2D(mImages[i], std::floor(std::log2(std::max(datas[i].width, datas[i].width))) + 1,
			GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, datas[i].width, datas[i].height);
		glTextureSubImage2D(mImages[i], 0, 0, 0, datas[i].width, datas[i].height, GL_RGBA, GL_UNSIGNED_BYTE, datas[i].data);

		stbi_image_free(datas[i].data);

		glGenerateTextureMipmap(mImages[i]);
	}

}

void ModelObject::loadTextures(const fastgltf::Expected<fastgltf::Asset>& asset)
{
	std::unordered_set<GLuint64> set{};

	mTextures.resize(asset->textures.size());
	for (int i{ 0 }; i < asset->textures.size(); ++i)
	{
		// imageIndex is guaranteed to have a value unless certain extensions are enabled
		mTextures[i].image = asset->textures[i].imageIndex.value();

		if (asset->textures[i].samplerIndex)
		{
			mTextures[i].sampler = asset->textures[i].samplerIndex.value();
			mTextures[i].bindlessHandle = glGetTextureSamplerHandleARB(mImages[mTextures[i].image], mSamplers[mTextures[i].sampler]);
		}
		else
		{
			mTextures[i].bindlessHandle = glGetTextureSamplerHandleARB(mImages[mTextures[i].image], mDefaultSampler);
		}

		// OpenGL complains when I try to make an already resident handle resident
		if (!set.contains(mTextures[i].bindlessHandle))
		{
			glMakeTextureHandleResidentARB(mTextures[i].bindlessHandle);
			set.insert(mTextures[i].bindlessHandle);
		}
	}
}

void ModelObject::loadMaterials(const fastgltf::Expected<fastgltf::Asset>& asset)
{
	mMaterials.resize(asset->materials.size());

	for (int i{ 0 }; i < asset->materials.size(); ++i)
	{
		glm::vec4 colorFactor
		{
		asset->materials[i].pbrData.baseColorFactor.x(),
		asset->materials[i].pbrData.baseColorFactor.y(),
		asset->materials[i].pbrData.baseColorFactor.z(),
		asset->materials[i].pbrData.baseColorFactor.w(),
		};

		bool hasColorTexture{ false };
		GLuint64 colorTexture{};
		bool hasMetallicRoughnessTexture{ false };
		GLuint64 metallicRoughnessTexture{};
		bool hasNormalTexture{ false };
		GLuint64 normalTexture{};

		if (asset->materials[i].pbrData.baseColorTexture)
		{
			hasColorTexture = true;

			colorTexture =
				asset->materials[i].pbrData.baseColorTexture.value().textureIndex;

			colorTexture = mTextures[colorTexture].bindlessHandle;
		}
		if (asset->materials[i].pbrData.metallicRoughnessTexture)
		{
			hasMetallicRoughnessTexture = true;

			metallicRoughnessTexture =
				asset->materials[i].pbrData.metallicRoughnessTexture.value().textureIndex;

			metallicRoughnessTexture = mTextures[metallicRoughnessTexture].bindlessHandle;
		}
		if (asset->materials[i].normalTexture)
		{
			hasNormalTexture = true;

			normalTexture = asset->materials[i].normalTexture.value().textureIndex;

			normalTexture = mTextures[normalTexture].bindlessHandle;
		}

		Material material
		{
			.colorFactor{ colorFactor },
			.colorTexture{ colorTexture },
			.normalTexture{ normalTexture },
			.metallicFactor{ asset->materials[i].pbrData.metallicFactor },
			.roughnessFactor{ asset->materials[i].pbrData.roughnessFactor },
			.hasColorTexture{ hasColorTexture },
			.hasNormalTexture{ hasNormalTexture },
			.alphaMask{ asset->materials[i].alphaMode == fastgltf::AlphaMode::Mask },
			.alphaCutoff{ asset->materials[i].alphaCutoff },
			.alphaBlend{ asset->materials[i].alphaMode == fastgltf::AlphaMode::Blend },
		};
		mMaterials[i] = material;
	}
}



void ModelObject::moveFrom(ModelObject&& o)
{
	mNodes = std::move(o.mNodes);
	mRootNodes = std::move(o.mRootNodes);;

	mMeshes = std::move(o.mMeshes);
	mPrimitiveCount = o.mPrimitiveCount;
	o.mPrimitiveCount = 0;

	mClusters = std::move(o.mClusters);

	mSamplers = std::move(o.mSamplers);
	mDefaultSampler = std::move(o.mDefaultSampler);
	mImages = std::move(o.mImages);
	mTextures = std::move(o.mTextures);

	mGlobalTransforms = std::move(o.mGlobalTransforms);
	mMaterials = std::move(o.mMaterials);

	mVertices = std::move(o.mVertices);
	mIndices = std::move(o.mIndices);

	mBlendIndexCount = o.mBlendIndexCount;
	o.mBlendIndexCount = 0;
}

void ModelObject::cleanup()
{
	for (const auto& texture : mTextures)
	{
		glMakeTextureHandleNonResidentARB(texture.bindlessHandle);
	}

	for (auto sampler : mSamplers)
	{
		glDeleteSamplers(1, &sampler);
	}
	glDeleteSamplers(1, &mDefaultSampler);

	for (auto image : mImages)
	{
		glDeleteTextures(1, &image);
	}
}