#include "camera.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/quaternion.hpp"

#include <cmath>

void Camera::move(const glm::vec3& displacement)
{
	mPos += getRotationMatrix() * glm::vec4{ displacement, 0.0f };
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