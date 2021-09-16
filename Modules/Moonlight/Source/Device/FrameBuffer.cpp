#include "FrameBuffer.h"
#include <CLog.h>

Moonlight::FrameBuffer::FrameBuffer(uint32_t width, uint32_t height)
	: Width(width)
	, Height(height)
{
	//uint32_t m_reset = 128;
	//uint32_t msaa = (m_reset & BGFX_RESET_MSAA_MASK) >> BGFX_RESET_MSAA_SHIFT;

	const uint64_t textureFlags = BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_TEXTURE_RT_MSAA_X16;// (uint64_t(msaa + 1) << BGFX_TEXTURE_RT_MSAA_SHIFT);

	bgfx::TextureFormat::Enum depthFormat =
		bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16, textureFlags) ? bgfx::TextureFormat::D16
		: bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D24S8, textureFlags) ? bgfx::TextureFormat::D24S8
		: bgfx::TextureFormat::D32;

	bgfx::TextureHandle fbtextures[] =
	{
		bgfx::createTexture2D(
				uint16_t(Width)
				, uint16_t(Height)
				, false
				, 1
				, bgfx::TextureFormat::BGRA8
				, BGFX_TEXTURE_RT_MSAA_X16 | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
			),
		bgfx::createTexture2D(
				uint16_t(Width)
				, uint16_t(Height)
				, false
				, 1
				, depthFormat
				, textureFlags
			),
		//bgfx::createTexture2D(
		//	  Width
		//	, Height
		//	, false
		//	, 1
		//	, bgfx::TextureFormat::RGBA8
		//	, BGFX_TEXTURE_RT | BGFX_STATE_DEPTH_TEST_LESS
		//	),
	};
	Texture = fbtextures[0];
	DepthTexture = fbtextures[1];
	Buffer = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, true);
}

Moonlight::FrameBuffer::~FrameBuffer()
{
	bgfx::destroy(Buffer);
	bgfx::destroy(Texture);
	bgfx::destroy(DepthTexture);
}

void Moonlight::FrameBuffer::Resize(Vector2 newSize)
{
	CLog::Log(CLog::LogType::Warning, "[TODO] Trying to resize frame buffer.");
}
