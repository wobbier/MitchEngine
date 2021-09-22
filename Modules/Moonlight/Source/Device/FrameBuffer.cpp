#include "FrameBuffer.h"
#include <CLog.h>

Moonlight::FrameBuffer::FrameBuffer(uint32_t width, uint32_t height)
	: Width(width)
	, Height(height)
	, Buffer(BGFX_INVALID_HANDLE)
{

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

void Moonlight::FrameBuffer::ReCreate(uint32_t resetFlags)
{
	m_resetFlags = resetFlags;
	uint32_t msaa = (m_resetFlags & BGFX_RESET_MSAA_MASK) >> BGFX_RESET_MSAA_SHIFT;

	if (bgfx::isValid(Buffer))
	{
		bgfx::destroy(Buffer);
	}

	const uint64_t textureFlags = BGFX_TEXTURE_RT_WRITE_ONLY | (uint64_t(msaa + 1) << BGFX_TEXTURE_RT_MSAA_SHIFT);
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
				, (uint64_t(msaa + 1) << BGFX_TEXTURE_RT_MSAA_SHIFT) | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
			),
		bgfx::createTexture2D(
				uint16_t(Width)
				, uint16_t(Height)
				, false
				, 1
				, depthFormat
				, textureFlags
			),
	};
	Texture = fbtextures[0];
	DepthTexture = fbtextures[1];
	Buffer = bgfx::createFrameBuffer(BX_COUNTOF(fbtextures), fbtextures, true);
}
