#pragma once
#include "bgfx/bgfx.h"
#include "bx/bx.h"

namespace Moonlight
{
	struct FrameBuffer
	{
		FrameBuffer(uint32_t width, uint32_t height);

		bgfx::FrameBufferHandle Buffer;
		bgfx::TextureHandle Texture;
		uint32_t Width = 0;
		uint32_t Height = 0;
	};
}