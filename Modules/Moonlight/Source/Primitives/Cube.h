#pragma once

#include <bgfx/bgfx.h>
#include <Graphics/ShaderStructures.h>
#include "Graphics/MeshData.h"

namespace Moonlight
{
	static Moonlight::PosColorVertex s_cubeVertices[] =
	{
		{{-1.0f,  1.0f,  1.0f}, 0xff000000 },
		{{ 1.0f,  1.0f,  1.0f}, 0xff0000ff },
		{{-1.0f, -1.0f,  1.0f}, 0xff00ff00 },
		{{ 1.0f, -1.0f,  1.0f}, 0xff00ffff },
		{{-1.0f,  1.0f, -1.0f}, 0xffff0000 },
		{{ 1.0f,  1.0f, -1.0f}, 0xffff00ff },
		{{-1.0f, -1.0f, -1.0f}, 0xffffff00 },
		{{ 1.0f, -1.0f, -1.0f}, 0xffffffff },
	};

	static const uint16_t s_cubeTriList[] =
	{
		0, 1, 2, // 0
		1, 3, 2,
		4, 6, 5, // 2
		5, 6, 7,
		0, 2, 4, // 4
		4, 2, 6,
		1, 5, 3, // 6
		5, 7, 3,
		0, 4, 1, // 8
		4, 5, 1,
		2, 3, 6, // 10
		6, 3, 7,
	};

	class CubeMesh
		: public Moonlight::MeshData
	{
	public:
		CubeMesh()
			: Moonlight::MeshData()
		{
			// top left
			Moonlight::PosNormTexTanBiVertex vert1;
			vert1.Position = { -1.0f,  1.0f,  1.0f };
			vert1.TextureCoord = { 0.f,0.f };
			Vertices.push_back(vert1);

			// bottom left
			Moonlight::PosNormTexTanBiVertex vert2;
			vert2.Position = { 1.0f,  1.0f,  1.0f };
			vert2.TextureCoord = { 0.f,1.f };
			Vertices.push_back(vert2);

			// top right
			Moonlight::PosNormTexTanBiVertex vert3;
			vert3.Position = { -1.0f, -1.0f,  1.0f };
			vert3.TextureCoord = { 1.f,0.f };
			Vertices.push_back(vert3);

			// bottom right
			Moonlight::PosNormTexTanBiVertex vert4;
			vert4.Position = { 1.0f, -1.0f,  1.0f };
			vert4.TextureCoord = { 1.f,1.f };
			Vertices.push_back(vert4);

			{
				Moonlight::PosNormTexTanBiVertex vert5;
				vert5.Position = { -1.0f,  1.0f, -1.0f };
				vert5.TextureCoord = { 1.f,1.f };
				Vertices.push_back(vert5);
			}
			{
				Moonlight::PosNormTexTanBiVertex vert5;
				vert5.Position = { 1.0f,  1.0f, -1.0f };
				vert5.TextureCoord = { 1.f,1.f };
				Vertices.push_back(vert5);
			}
			{
				Moonlight::PosNormTexTanBiVertex vert5;
				vert5.Position = { -1.0f, -1.0f, -1.0f };
				vert5.TextureCoord = { 1.f,1.f };
				Vertices.push_back(vert5);
			}
			{
				Moonlight::PosNormTexTanBiVertex vert5;
				vert5.Position = { 1.0f, -1.0f, -1.0f };
				vert5.TextureCoord = { 1.f,1.f };
				Vertices.push_back(vert5);
			}

			Indices = {
				0, 1, 2, // 0
				1, 3, 2,
				4, 6, 5, // 2
				5, 6, 7,
				0, 2, 4, // 4
				4, 2, 6,
				1, 5, 3, // 6
				5, 7, 3,
				0, 4, 1, // 8
				4, 5, 1,
				2, 3, 6, // 10
				6, 3, 7,
			};

			//VertexPositionTexCoord verts[5];
			//verts[0].Position = vert1.Position;
			//verts[1].Position = vert2.Position;
			//verts[2].Position = vert3.Position;
			//verts[3].Position = vert4.Position;
			//verts[4].Position = vert2.Position;

			//verts[0].TexCoord = vert1.TexCoord;
			//verts[1].TexCoord = vert2.TexCoord;
			//verts[2].TexCoord = vert3.TexCoord;
			//verts[3].TexCoord = vert4.TexCoord;
			//verts[4].TexCoord = vert2.TexCoord;
			//this->vertices.push_back()
			//MeshMaterial = MakeShared<DiffuseMaterial>();

			InitMesh();
		}
	};

	static void screenSpaceQuad(float _textureWidth, float _textureHeight, float _texelHalf, bool _originBottomLeft, float _width = 1.0f, float _height = 1.0f)
	{
		if (3 == bgfx::getAvailTransientVertexBuffer(3, PosTexCoordVertex::ms_layout))
		{
			bgfx::TransientVertexBuffer vb;
			bgfx::allocTransientVertexBuffer(&vb, 3, PosTexCoordVertex::ms_layout);
			PosTexCoordVertex* vertex = (PosTexCoordVertex*)vb.data;

			const float minx = -_width;
			const float maxx = _width;
			const float miny = 0.0f;
			const float maxy = _height * 2.0f;

			const float texelHalfW = _texelHalf / _textureWidth;
			const float texelHalfH = _texelHalf / _textureHeight;
			const float minu = -1.0f + texelHalfW;
			const float maxu = 1.0f + texelHalfW;

			const float zz = 0.0f;

			float minv = texelHalfH;
			float maxv = 2.0f + texelHalfH;

			if (_originBottomLeft)
			{
				float temp = minv;
				minv = maxv;
				maxv = temp;

				minv -= 1.0f;
				maxv -= 1.0f;
			}

			vertex[0].vec.x = minx;
			vertex[0].vec.y = miny;
			vertex[0].vec.z = zz;
			vertex[0].coords.x = minu;
			vertex[0].coords.y = minv;

			vertex[1].vec.x = maxx;
			vertex[1].vec.y = miny;
			vertex[1].vec.z = zz;
			vertex[1].coords.x = maxu;
			vertex[1].coords.y = minv;

			vertex[2].vec.x = maxx;
			vertex[2].vec.y = maxy;
			vertex[2].vec.z = zz;
			vertex[2].coords.x = maxu;
			vertex[2].coords.y = maxv;

			bgfx::setVertexBuffer(0, &vb);
		}
	}

}
