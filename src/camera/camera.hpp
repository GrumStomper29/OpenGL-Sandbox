#pragma once

#include "glm/glm.hpp"

class Camera final
{
public:

	Camera() = default;
	Camera(const glm::vec3& pos, const glm::vec2& rot = { 0.0f, -90.0f }, float fov = 90.0f)
		: mPos{ pos }
		, mRot{ rot }
		, mFov{ fov }
	{
	}

	void move(const glm::vec3& displacement);

	glm::mat4 getViewMatrix();

	glm::vec3 mPos{};
	glm::vec2 mRot{ 0.0f, -90.0f };
	float mFov{ 75.0f };

private:

	glm::mat4 getRotationMatrix();

};