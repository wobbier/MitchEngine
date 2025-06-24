#pragma once
#pragma message("[MAN] Compiling Engine PCH...")

#include "Dementia.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <SDKDDKVer.h>
#include <Windows.h>
#endif

#include "Pointers.h"

#if USING( ME_PLATFORM_UWP )
//#include <agile.h>
#endif

#include "optick.h"

// Math
#include <glm/glm.hpp>
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Math/Matrix4.h"
#include "Mathf.h"

#if USING( ME_IMGUI )
#include <imgui.h>
#include "misc/cpp/imgui_stdlib.h"
#include "Utils/HavanaUtils.h"
#endif

//#include "ozz/animation/runtime/animation.h"
//#include "ozz/animation/runtime/blending_job.h"
//#include "ozz/animation/runtime/local_to_model_job.h"
//#include "ozz/animation/runtime/sampling_job.h"
//#include "ozz/animation/runtime/skeleton.h"
//#include "ozz/animation/runtime/skeleton_utils.h"
//
//#include "ozz/base/log.h"
//
//#include "ozz/base/containers/vector.h"
//
//#include "ozz/base/maths/simd_math.h"
//#include "ozz/base/maths/soa_transform.h"
//#include "ozz/base/maths/vec_float.h"
//
//#include "ozz/base/memory/allocator.h"
//
//#include "ozz/options/options.h"

// std
#include <string>
#include <algorithm>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <iostream>
#include <optional>

#include <JSON.h>

#include "Utils/StringUtils.h"
#include "Path.h"

//#include "ECS/ComponentDetail.h"
#include "Core/Assert.h"

#include <CLog.h>

// core
#include "Game.h"
