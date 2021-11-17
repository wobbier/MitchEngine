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
		TextureType Type;

		int mWidth = 1920;
		int mHeight = 1080;
		int mChannels;
		Texture() = delete;
		Texture(const Path& InFilePath, WrapMode mode = WrapMode::Wrap);
		Texture(FrameBuffer* InFilePath, WrapMode mode = WrapMode::Wrap);
		~Texture();

		virtual void Load() override;
		virtual void Reload() override;

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
	enum class OutputTextureType : uint8_t
	{
		Default = 0,
		NormalMap,
		Sprite,
		Count
	};

	TextureResourceMetadata(const Path& filePath)
		: MetaBase(filePath)
	{
	}

	virtual std::string GetExtension2() const override;

	void OnSerialize(json& inJson) override;
	void OnDeserialize(const json& inJson) override;

	bool GenerateMIPs = true;
	bool GenerateSpriteMIPs = false;
	OutputTextureType OutputType = OutputTextureType::Default;

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif

#if ME_EDITOR || defined(ME_TOOLS)
	void Export() override;
#endif

private:
	std::string FromEnum(OutputTextureType inType);
	OutputTextureType ToEnum(std::string inType);
};

struct TextureResourceMetadataJpg
    : public TextureResourceMetadata
{
    TextureResourceMetadataJpg(const Path& filePath) : TextureResourceMetadata(filePath) {}
};


ME_REGISTER_METADATA("png", TextureResourceMetadata);
ME_REGISTER_METADATA("jpg", TextureResourceMetadataJpg);
