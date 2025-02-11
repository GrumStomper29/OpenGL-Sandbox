#include "camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_access.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"

#include <cmath>

void Camera::move(const glm::vec3& displacement)
{
	mPos += getRotationMatrix() * glm::vec4{ displacement, 0.0f };
}

Camera::Frustum Camera::getViewFrustum(const glm::mat4& proj)
{
	glm::mat4 mat{ proj * getViewMatrix() };

	Frustum frustum{};

	frustum.left   = glm::row(mat, 3) + glm::row(mat, 0);
	frustum.right  = glm::row(mat, 3) - glm::row(mat, 0);
	frustum.bottom = glm::row(mat, 3) + glm::row(mat, 1);
	frustum.top    = glm::row(mat, 3) - glm::row(mat, 1);
	frustum.near   = glm::row(mat, 3) + glm::row(mat, 2);
	frustum.far    = glm::row(mat, 3) - glm::row(mat, 2);

	auto normalizePlane{ [](glm::vec4& p) {
			float mag{ std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z) };
			p /= mag;
		} };

	normalizePlane(frustum.left);
	normalizePlane(frustum.right);
	normalizePlane(frustum.bottom);
	normalizePlane(frustum.top);
	normalizePlane(frustum.near);
	normalizePlane(frustum.far);

	return frustum;
}

glm::mat4 Camera::getViewMatrix()
{
	glm::mat4 translation{ glm::translate({ 1.0f }, mPos) };
	glm::mat4 rotation{ getRotationMatrix() };
	return glm::inverse(translation * rotation);
}

glm::mat4 Camera::getRotationMatrix()
{
	glm::quat xRotationQuat{ glm::angleAxis(glm::radians(mRot.x), glm::vec3{ 1.0f, 0.0f, 0.0f }) };
	glm::quat yRotationQuat{ glm::angleAxis(glm::radians(mRot.y), glm::vec3{ 0.0f, -1.0f, 0.0f }) };
	return glm::toMat4(yRotationQuat) * glm::toMat4(xRotationQuat);
}