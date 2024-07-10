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

#include <algorithm> // for transform & max
#include <cmath>
#include <cstddef> // for size_t
#include <cstdint>
#include <execution> // for std::execution::par
#include <filesystem>
#include <iostream>
#include <unordered_set>
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



ModelObject::ModelObject(const std::filesystem::path& path, const std::filesystem::path& directory)
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

	loadMeshes(asset);

	loadSamplers(asset);
	loadImages(asset);

	loadTextures(asset);

	loadMaterials(asset);

	uploadGeometry();
}

ModelObject::~ModelObject()
{
	for (const auto& mesh : mMeshes)
	{
		for (const auto& primitive : mesh.primitives)
		{
			glDeleteBuffers(1, &primitive.drawIndirectBuffer);
		}
	}

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

	for (const auto& material : mMaterials)
	{
		glDeleteBuffers(1, &material.ubo);
	}

	glDeleteVertexArrays(1, &mVao);
	glDeleteBuffers(1, &mIbo);
	glDeleteBuffers(1, &mVbo);
}


void ModelObject::draw(const glm::mat4& transform, GLuint shaderProgram, AlphaMode alphaMode, int& triangles, int& draws)
{
	glBindVertexArray(mVao);
	for (auto rootNodeIndex : mRootNodes)
	{
		renderNodeAndChildren(mNodes[rootNodeIndex], transform, shaderProgram, alphaMode, triangles, draws);
	}
}

void ModelObject::renderNodeAndChildren(const Node& node, const glm::mat4& parentTransform, GLuint shaderProgram, 
	AlphaMode alphaMode, int& triangles, int& draws)
{
	if (node.mesh != -1)
	{
		for (const auto& primitive : mMeshes[node.mesh].primitives)
		{
			if (primitive.material != -1)
			{
				const auto& material{ mMaterials[primitive.material] };

				if (!(material.alphaMode & alphaMode))
				{
					continue;
				}

				glBindBufferBase(GL_UNIFORM_BUFFER, 1, material.ubo);

				if (!material.doubleSided)
				{
					glEnable(GL_CULL_FACE);
				}
				else
				{
					glDisable(GL_CULL_FACE);
				}
			}

			auto loc{ glGetUniformLocation(shaderProgram, "model") };
			glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(parentTransform * node.localTransform));

			glBindBuffer(GL_DRAW_INDIRECT_BUFFER, primitive.drawIndirectBuffer);
			glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);

			triangles += primitive.indexCount / 3;
			draws++;
		}
	}

	for (auto childIndex : node.children)
	{
		renderNodeAndChildren(mNodes[childIndex], parentTransform * node.localTransform, shaderProgram,
			alphaMode, triangles, draws);
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

void ModelObject::loadMeshes(fastgltf::Expected<fastgltf::Asset>& asset)
{
	mMeshes.resize(asset->meshes.size());
	for (int i{ 0 }; i < asset->meshes.size(); ++i)
	{
		mMeshes[i].primitives.resize(asset->meshes[i].primitives.size());

		for (int n{ 0 }; n < asset->meshes[i].primitives.size(); ++n)
		{
			std::size_t vertOffset{ mVertices.size() };

			Primitive& newPrimitive{ mMeshes[i].primitives[n] };

			{
				auto accessor{ asset->accessors[asset->meshes[i].primitives[n].indicesAccessor.value()] };

				std::size_t indexOffset{ mIndices.size() };
				newPrimitive.indexOffset = indexOffset;

				mIndices.resize(mIndices.size() + accessor.count);

				newPrimitive.indexCount = accessor.count;

				fastgltf::iterateAccessorWithIndex<std::uint32_t>(asset.get(), accessor, [&](std::uint32_t val, std::size_t k) {
					mIndices[indexOffset + k] = val + vertOffset;
					});
			}

			{
				auto accessor{ asset->accessors[asset->meshes[i].primitives[n].findAttribute("POSITION")->second] };

				mVertices.resize(mVertices.size() + accessor.count);

				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), accessor, [&](glm::vec3 v, std::size_t k) {
					mVertices[vertOffset + k].pos = v;
					});
			}

			{
				auto normals{ asset->meshes[i].primitives[n].findAttribute("NORMAL") };

				if (normals != asset->meshes[i].primitives[n].attributes.end())
				{
					auto accessor{ asset->accessors[normals->second] };

					fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), accessor, [&](glm::vec3 v, std::size_t k) {
						mVertices[vertOffset + k].normal = v;
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
						mVertices[vertOffset + k].uv = v;
						});

				}
			}

			newPrimitive.material = asset->meshes[i].primitives[n].materialIndex.value_or(-1);
			
			DrawIndirect indirect
			{
				.count{ static_cast<GLuint>(newPrimitive.indexCount) },
				.instanceCount{ 1 },
				.firstIndex{ static_cast<GLuint>(newPrimitive.indexOffset) },
				.baseVertex{ 0 },
				.baseInstance{ 0 },
			};

			glCreateBuffers(1, &newPrimitive.drawIndirectBuffer);
			glNamedBufferData(newPrimitive.drawIndirectBuffer, sizeof(DrawIndirect), &indirect, GL_STATIC_DRAW);
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

		// OpenGL complains when we try to make an already resident handle resident
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
		mMaterials[i].colorFactor =
		{
			asset->materials[i].pbrData.baseColorFactor.x(),
			asset->materials[i].pbrData.baseColorFactor.y(),
			asset->materials[i].pbrData.baseColorFactor.z(),
			asset->materials[i].pbrData.baseColorFactor.w(),
		};
		mMaterials[i].metallicFactor = asset->materials[i].pbrData.metallicFactor;
		mMaterials[i].roughnessFactor = asset->materials[i].pbrData.roughnessFactor;
		
		if (asset->materials[i].pbrData.baseColorTexture)
		{
			mMaterials[i].colorTexture =
				asset->materials[i].pbrData.baseColorTexture.value().textureIndex;
		}
		if (asset->materials[i].pbrData.metallicRoughnessTexture)
		{
			mMaterials[i].metallicRoughnessTexture =
				asset->materials[i].pbrData.metallicRoughnessTexture.value().textureIndex;
		}
		if (asset->materials[i].normalTexture)
		{
			mMaterials[i].normalTexture = asset->materials[i].normalTexture.value().textureIndex;
		}
		
		mMaterials[i].doubleSided = asset->materials[i].doubleSided;
		switch (asset->materials[i].alphaMode)
		{
		case fastgltf::AlphaMode::Mask:
			mMaterials[i].alphaMode = MASK;
			break;
		case fastgltf::AlphaMode::Blend:
			mMaterials[i].alphaMode = BLEND;
			break;
		default:
			mMaterials[i].alphaMode = OPAQUE;
			break;
		}

		GLuint64 colorTextureHandle{ 0 };
		if (mMaterials[i].colorTexture != -1)
		{
			colorTextureHandle = mTextures[mMaterials[i].colorTexture].bindlessHandle;
		}

		MaterialUbo uboData
		{
			.colorFactor{ mMaterials[i].colorFactor },
			.metallicFactor{ mMaterials[i].metallicFactor },
			.roughnessFactor{ mMaterials[i].roughnessFactor },
			.hasColorTexture{ mMaterials[i].colorTexture != -1 },
			.colorTexture{ mMaterials[i].colorTexture != -1 ? mTextures[mMaterials[i].colorTexture].bindlessHandle : 0 },
			.hasNormalTexture{ mMaterials[i].normalTexture != -1 },
			.normalTexture{ mMaterials[i].normalTexture != -1 ? mTextures[mMaterials[i].normalTexture].bindlessHandle : 0 },
			.alphaMask{ mMaterials[i].alphaMode == MASK },
			.alphaCutoff{ asset->materials[i].alphaCutoff },
		};

		glCreateBuffers(1, &mMaterials[i].ubo);
		glNamedBufferData(mMaterials[i].ubo, sizeof(MaterialUbo), &uboData, GL_STATIC_DRAW);
	}
}

void ModelObject::uploadGeometry()
{
	glCreateBuffers(1, &mVbo);
	glNamedBufferStorage(mVbo, sizeof(Vertex) * mVertices.size(), mVertices.data(), GL_NONE);

	glCreateBuffers(1, &mIbo);
	glNamedBufferStorage(mIbo, sizeof(std::uint32_t) * mIndices.size(), mIndices.data(), GL_NONE);

	glCreateVertexArrays(1, &mVao);

	glVertexArrayVertexBuffer(mVao, 0, mVbo, 0, sizeof(Vertex));
	glVertexArrayElementBuffer(mVao, mIbo);

	glEnableVertexArrayAttrib(mVao, 0);
	glEnableVertexArrayAttrib(mVao, 1);
	glEnableVertexArrayAttrib(mVao, 2);

	glVertexArrayAttribFormat(mVao, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribFormat(mVao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
	glVertexArrayAttribFormat(mVao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));

	glVertexArrayAttribBinding(mVao, 0, 0);
	glVertexArrayAttribBinding(mVao, 1, 0);
	glVertexArrayAttribBinding(mVao, 2, 0);
}