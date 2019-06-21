#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"
#include <d3d11.h>
#include <wrl/client.h>
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

	class Texture
		: public Resource
	{
	public:
		unsigned int Id;
		TextureType Type;

		int mWidth;
		int mHeight;
		int mChannels;

		Texture(const Path& InFilePath, int levels = 0);
		~Texture();

		template<typename T> static constexpr T NumMipmapLevels(T width, T height)
		{
			T levels = 1;
			while ((width | height) >> levels) {
				++levels;
			}
			return levels;
		}

		// Textures should not be copied around in memory
		ME_NONCOPYABLE(Texture);

		ID3D11ShaderResourceView* ShaderResourceView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> SamplerState;
		ID3D11Resource* resource;

		static std::string ToString(TextureType type);
		static std::wstring ToStringW(const std::string& strText);
	};
}