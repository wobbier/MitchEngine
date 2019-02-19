#pragma once
#include <string>
#include "Dementia.h"
#include "Resource/Resource.h"
#include <d3d11.h>
namespace Moonlight
{
	enum class TextureType : unsigned short
	{
		Diffuse = 0,
		Normal,
		Specular,
		Height
	};
	class Texture : public Resource
	{
	public:
		unsigned int Id;
		TextureType Type;
		std::string Directory;

		int Width;
		int Height;

		Texture();
		~Texture();

		static Texture* Load(const FilePath& InFilePath);

		// Textures should not be copied around in memory
		ME_NONCOPYABLE(Texture);
		int nrChannels;
		std::string path;

		ID3D11ShaderResourceView* CubesTexture;
		ID3D11SamplerState* CubesTexSamplerState;
		ID3D11Resource* resource;

		static std::wstring ToStringW(const std::string& strText);
	};
}