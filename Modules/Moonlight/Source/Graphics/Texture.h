#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"
#include "bx/allocator.h"
#include "bx/readerwriter.h"
#include "bx/file.h"
#include "bgfx/bgfx.h"
#include <JSON.h>
#include <Resource/MetaRegistry.h>

namespace Moonlight { struct FrameBuffer; }

namespace Moonlight
{


	enum TextureType
	{
		Diffuse = 0,
		Normal,
		Specular,
		Height,
		Opacity,
		Count
	};

	enum WrapMode
	{
		Clamp,
		Decal,
		Wrap,
		Mirror
	};

	class Texture
		: public Resource
	{
	public:
		unsigned int Id;
		TextureType Type;

		int mWidth;
		int mHeight;
		int mChannels;

		Texture(const Path& InFilePath, WrapMode mode = WrapMode::Wrap);
		Texture(FrameBuffer* InFilePath, WrapMode mode = WrapMode::Wrap);
		~Texture();

		void UpdateBuffer(FrameBuffer* NewBuffer);

		template<typename T> static constexpr T NumMipmapLevels(T width, T height)
		{
			T levels = 1;
			while ((width | height) >> levels) {
				++levels;
			}
			return levels;
		}

		bx::FileReaderI* s_fileReader = nullptr;
		bx::FileWriterI* s_fileWriter = nullptr/* = BX_NEW(s_allocator, Moonlight::FileWriter)*/;
		bx::AllocatorI* getDefaultAllocator();

		// Textures should not be copied around in memory
		ME_NONCOPYABLE(Texture);

		bgfx::TextureHandle TexHandle;
		static std::string ToString(TextureType type);
	};
}

struct TextureResourceMetadata
	: public MetaBase
{
	TextureResourceMetadata(const Path& filePath) : MetaBase(filePath) {}

	void OnSerialize(json& inJson) override;
	void OnDeserialize(const json& inJson) override;

#if ME_EDITOR && ME_PLATFORM_WIN64
	void Export() override;

	virtual void OnEditorInspect() final;
#endif
};

ME_REGISTER_METADATA("png", TextureResourceMetadata);
