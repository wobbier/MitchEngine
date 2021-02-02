#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "bx/allocator.h"
#include "bx/readerwriter.h"
#include "bx/file.h"
#include "bgfx/bgfx.h"

namespace Moonlight { struct FrameBuffer; }

namespace Moonlight
{
	static bx::FileReaderI* s_fileReader;
	static bx::FileWriterI* s_fileWriter;

	static bx::AllocatorI* s_allocator3 = new bx::DefaultAllocator();
	typedef bx::StringT<&s_allocator3> String;
	static String s_currentDir;

	class FileReader : public bx::FileReader
	{
		typedef bx::FileReader super;

	public:
		virtual bool open(const bx::FilePath& _filePath, bx::Error* _err) override
		{
			String filePath(s_currentDir);
			filePath.append(_filePath);
			return super::open(filePath.getPtr(), _err);
		}
	};

	class FileWriter : public bx::FileWriter
	{
		typedef bx::FileWriter super;

	public:
		virtual bool open(const bx::FilePath& _filePath, bool _append, bx::Error* _err) override
		{
			String filePath(s_currentDir);
			filePath.append(_filePath);
			return super::open(filePath.getPtr(), _append, _err);
		}
	};

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

		ID3D11ShaderResourceView* ShaderResourceView = nullptr;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> SamplerState;
		ID3D11Resource* resource = nullptr;
		bgfx::TextureHandle TexHandle;
		static std::string ToString(TextureType type);
	};
}