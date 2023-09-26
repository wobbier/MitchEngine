#pragma once
#include "Device/DX11Device.h"

namespace Moonlight
{
    class RenderTexture
    {
    public:
        RenderTexture()// DX11Device* device)
        {
            //D3D11_SAMPLER_DESC sampDesc;
            //ZeroMemory(&sampDesc, sizeof(sampDesc));
            //sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            //sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            //sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            //sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            //sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
            //sampDesc.MinLOD = 0;
            //sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
            //device->GetD3DDevice()->CreateSamplerState(&sampDesc, &CubesTexSamplerState);

            //D3D11_TEXTURE2D_DESC textureDesc;
            //D3D11_RENDER_TARGET_VIEW_DESC1 renderTargetViewDesc;
            //D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;

            /////////////////////////// Map's Texture
            //// Initialize the  texture description.
            //ZeroMemory(&textureDesc, sizeof(textureDesc));

            //// Setup the texture description.
            //// We will have our map be a square
            //// We will need to have this texture bound as a render target AND a shader resource
            //Size = device->GetOutputSize();
            //textureDesc.Width = static_cast<UINT>(device->GetOutputSize().X());
            //textureDesc.Height = static_cast<UINT>(device->GetOutputSize().Y());
            //textureDesc.MipLevels = 1;
            //textureDesc.ArraySize = 1;
            //textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            //textureDesc.SampleDesc.Count = 1;
            //textureDesc.Usage = D3D11_USAGE_DEFAULT;
            //textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            //textureDesc.CPUAccessFlags = 0;
            //textureDesc.MiscFlags = 0;

            //// Create the texture
            //device->GetD3DDevice()->CreateTexture2D(&textureDesc, NULL, &renderTargetTextureMap);

            ///////////////////////// Map's Render Target
            //// Setup the description of the render target view.
            //ZeroMemory(&renderTargetViewDesc, sizeof(renderTargetViewDesc));
            //renderTargetViewDesc.Format = textureDesc.Format;
            //renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            //renderTargetViewDesc.Texture2D.MipSlice = 0;

            //DX::ThrowIfFailed(
            //	device->GetD3DDevice()->CreateRenderTargetView1(
            //		renderTargetTextureMap,
            //		&renderTargetViewDesc,
            //		&renderTargetViewMap
            //	)
            //);

            //ZeroMemory(&shaderResourceViewDesc, sizeof(shaderResourceViewDesc));
            ///////////////////////// Map's Shader Resource View
            //// Setup the description of the shader resource view.
            //shaderResourceViewDesc.Format = textureDesc.Format;
            //shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            //shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
            //shaderResourceViewDesc.Texture2D.MipLevels = 1;

            //// Create the shader resource view.
            //device->GetD3DDevice()->CreateShaderResourceView(renderTargetTextureMap, &shaderResourceViewDesc, &shaderResourceViewMap);

        }
        ~RenderTexture()
        {
            //CubesTexSamplerState->Release();
            //renderTargetViewMap->Release();
            //renderTargetTextureMap->Release();
            //shaderResourceViewMap->Release();
        }

        void Resize( Vector2 newSize )
        {
            //D3D11_TEXTURE2D_DESC desc;
            //renderTargetTextureMap->GetDesc(&desc);
            //desc.Width = static_cast<UINT>(newSize.X());
            //desc.Height = static_cast<UINT>(newSize.Y());
        }
        Vector2 Size;
        //ID3D11SamplerState* CubesTexSamplerState;
        //ID3D11RenderTargetView1* renderTargetViewMap = nullptr;
        //ID3D11Texture2D* renderTargetTextureMap = nullptr;
        //ID3D11ShaderResourceView* shaderResourceViewMap = nullptr;
    };
}