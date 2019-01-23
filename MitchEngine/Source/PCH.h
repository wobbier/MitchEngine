#pragma once
#pragma message("[MAN] Compiling Engine PCH...")

#include <SDKDDKVer.h>

#include "Dementia.h"

#include <windows.h>
#if ME_DIRECTX
#include <wrl.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <d3d11_3.h>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <concrt.h>
#endif
#if ME_PLATFORM_UWP
#include <agile.h>
#endif

#include "Brofiler.h"

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

// glm
#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <gtc/type_ptr.hpp>
#include <gtx/rotate_vector.hpp>
#include <gtx/quaternion.hpp>

// core
#include "Game.h"