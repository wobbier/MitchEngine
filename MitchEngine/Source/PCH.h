#pragma once
#pragma message("[MAN] Compiling Engine PCH...")

// std
#include <string>
#include <algorithm>
#include <vector>

// glm
#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/quaternion.hpp>

#include "Utility/Util.h"
#include "Brofiler.h"
// core
#include "Game.h"