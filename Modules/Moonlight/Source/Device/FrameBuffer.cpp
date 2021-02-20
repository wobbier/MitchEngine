#pragma once
#include "FrameBuffer.h"

Moonlight::FrameBuffer::FrameBuffer(uint32_t width, uint32_t height)
	: Width(width)
	, Height(height)
{
	bgfx::TextureHandle fbtextures[] =
	{
		bgfx::createTexture2D(
			  Width
			, Height
			, false
			, 1
			, bgfx::TextureFormat::RGBA8
			, BGFX_TEXTURE_RT
			),
	};
	Texture = fbtextures[0];
	Buffer = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, true);
}
