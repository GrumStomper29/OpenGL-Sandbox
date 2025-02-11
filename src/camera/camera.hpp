#pragma once

#include "glm/glm.hpp"

class Camera final
{
public:

	struct Frustum
	{
		glm::vec4 top{};
		glm::vec4 bottom{};

		glm::vec4 right{};
		glm::vec4 left{};

		glm::vec4 far{};
		glm::vec4 near{};
	};

	Camera() = default;
	Camera(const glm::vec3& pos, const glm::vec2& rot = { 0.0f, -90.0f }, float fov = 90.0f)
		: mPos{ pos }
		, mRot{ rot }
		, mFov{ fov }
	{
	}

	void move(const glm::vec3& displacement);

	Frustum getViewFrustum(const glm::mat4& proj);

	glm::mat4 getViewMatrix();

	glm::vec3 mPos{};
	glm::vec2 mRot{ 0.0f, -90.0f };

	float mFov{ 75.0f };

	float mZNear{ 0.25f };
	float mZFar{ 10000.0f };

private:

	glm::mat4 getRotationMatrix();

};