#pragma once
#include "Graphics/MeshData.h"
#include "Graphics/ShaderStructures.h"
#include "Materials/DiffuseMaterial.h"

class PlaneMesh
    : public Moonlight::MeshData
{
public:
    PlaneMesh()
        : Moonlight::MeshData()
    {
        // top left
        Moonlight::PosNormTexTanBiVertex vert1;
        vert1.Position = { -1.f,0.f,-1.f };
        vert1.TextureCoord = { 0.f,0.f };
        Vertices.push_back( vert1 );

        // bottom left
        Moonlight::PosNormTexTanBiVertex vert2;
        vert2.Position = { -1.f, 0.f, 1.f };
        vert2.TextureCoord = { 0.f,1.f };
        Vertices.push_back( vert2 );

        // top right
        Moonlight::PosNormTexTanBiVertex vert3;
        vert3.Position = { 1.f, 0.f, -1.f };
        vert3.TextureCoord = { 1.f,0.f };
        Vertices.push_back( vert3 );

        // bottom right
        Moonlight::PosNormTexTanBiVertex vert4;
        vert4.Position = { 1.f, 0.f, 1.f };
        vert4.TextureCoord = { 1.f,1.f };
        Vertices.push_back( vert4 );

        Indices = { 0, 1, 2, 2, 1, 3 };

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
        MeshMaterial = MakeShared<DiffuseMaterial>();

        InitMesh();
    }
};