#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"
#include <d3d11.h>
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

		int Width;
		int Height;

		Texture(const FilePath& InFilePath);
		~Texture();

		// Textures should not be copied around in memory
		ME_NONCOPYABLE(Texture);

		ID3D11ShaderResourceView* CubesTexture;
		ID3D11SamplerState* CubesTexSamplerState;
		ID3D11Resource* resource;

		static std::string ToString(TextureType type);
		static std::wstring ToStringW(const std::string& strText);
	};
}