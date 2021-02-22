#pragma once
#pragma message("[MAN] Compiling Engine PCH...")


#include <SDKDDKVer.h>
#include <Windows.h>
#include "Dementia.h"
#include "Pointers.h"
#if ME_DIRECTX
#include <wrl.h>
#include <wrl/client.h>
#include <dxgi1_4.h>
#include <d3d11_3.h>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <concrt.h>

#endif
#if ME_PLATFORM_UWP
#include <agile.h>
#endif

#include "optick.h"

#include "Math/Vector2.h"

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

#include <nlohmann/json.hpp>

//#include "ECS/ComponentDetail.h"

#include <CLog.h>

// for convenience
using json = nlohmann::json;
// core
#include "Game.h"