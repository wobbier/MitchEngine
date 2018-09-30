#pragma once
#pragma message("[MAN] Compiling Engine PCH...")

// std
#include <string>
#include <algorithm>
#include <vector>

// windows
#include <wrl.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <memory>
#include <concrt.h>

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