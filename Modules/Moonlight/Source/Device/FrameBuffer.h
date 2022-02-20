#pragma once
#include "bgfx/bgfx.h"
#include "bx/bx.h"
#include "Math/Vector2.h"

namespace Moonlight
{
	struct FrameBuffer
	{
		FrameBuffer(uint32_t width, uint32_t height);
		~FrameBuffer();

		void Resize(Vector2 newSize);

		bgfx::FrameBufferHandle Buffer;
		bgfx::TextureHandle Texture;
		bgfx::TextureHandle DepthTexture;
		uint32_t Width = 0;
		uint32_t Height = 0;
		void ReCreate(uint32_t resetFlags);
		uint32_t m_resetFlags = 0;
        bool MatchMainBufferSize = false;
	};
}