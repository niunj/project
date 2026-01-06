#pragma once


#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>


#include <iostream>

namespace Sample
{
   inline std::ostream& operator <<(std::ostream& o, const glm::vec3& v)
   {
      o << "Vector3(" << v.x << ", " << v.y << ", " << v.z << ")";
      return o;
   }

   inline std::ostream& operator <<(std::ostream& o, const glm::vec4& v)
   {
      o << "Vector4(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
      return o;
   }
}