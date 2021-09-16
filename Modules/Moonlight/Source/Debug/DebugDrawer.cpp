#include "DebugDrawer.h"
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include "vs_debugdraw_lines.bin.h"
#include "fs_debugdraw_lines.bin.h"
#include "bx/bx.h"
#include <bx/math.h>
#include <Utils/BGFXUtils.h>

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_debugdraw_lines),
	BGFX_EMBEDDED_SHADER(fs_debugdraw_lines),

	BGFX_EMBEDDED_SHADER_END()
};

bgfx::VertexLayout DebugVertex::ms_layout;

struct DebugShapeVertex
{
	float m_x;
	float m_y;
	float m_z;
	uint8_t m_indices[4];

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8)
			.end();
	}

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout DebugShapeVertex::ms_layout;

static DebugShapeVertex s_cubeVertices[8] =
{
	{-1.0f,  1.0f,  1.0f, { 0, 0, 0, 0 } },
	{ 1.0f,  1.0f,  1.0f, { 0, 0, 0, 0 } },
	{-1.0f, -1.0f,  1.0f, { 0, 0, 0, 0 } },
	{ 1.0f, -1.0f,  1.0f, { 0, 0, 0, 0 } },
	{-1.0f,  1.0f, -1.0f, { 0, 0, 0, 0 } },
	{ 1.0f,  1.0f, -1.0f, { 0, 0, 0, 0 } },
	{-1.0f, -1.0f, -1.0f, { 0, 0, 0, 0 } },
	{ 1.0f, -1.0f, -1.0f, { 0, 0, 0, 0 } },
};

static const uint16_t s_cubeIndices[36] =
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

DebugDrawer::DebugDrawer()
{
	DebugShapeVertex::init();
	DebugVertex::init();

	bgfx::RendererType::Enum type = bgfx::getRendererType();

	m_program[Program::Lines] = bgfx::createProgram(
		bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_debugdraw_lines")
		, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_debugdraw_lines")
		, true
	);

	u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, 4);
	s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

	uint32_t startVertex = 0;
	uint32_t startIndex = 0;
	uint16_t stride = DebugShapeVertex::ms_layout.getStride();

	m_mesh[DebugMesh::Cube].m_startVertex = startVertex;
	m_mesh[DebugMesh::Cube].m_numVertices = BX_COUNTOF(s_cubeVertices);
	m_mesh[DebugMesh::Cube].m_startIndex[0] = startIndex;
	m_mesh[DebugMesh::Cube].m_numIndices[0] = BX_COUNTOF(s_cubeIndices);
	m_mesh[DebugMesh::Cube].m_startIndex[1] = 0;
	m_mesh[DebugMesh::Cube].m_numIndices[1] = 0;
	startVertex += m_mesh[DebugMesh::Cube].m_numVertices;
	startIndex += m_mesh[DebugMesh::Cube].m_numIndices[0];

	const bgfx::Memory* vb = bgfx::alloc(startVertex * stride);
	const bgfx::Memory* ib = bgfx::alloc(startIndex * sizeof(uint16_t));

	bx::memCopy(&vb->data[m_mesh[DebugMesh::Cube].m_startVertex * stride]
		, s_cubeVertices
		, sizeof(s_cubeVertices)
	);

	bx::memCopy(&ib->data[m_mesh[DebugMesh::Cube].m_startIndex[0] * sizeof(uint16_t)]
		, s_cubeIndices
		, sizeof(s_cubeIndices)
	);

	m_vbh = bgfx::createVertexBuffer(vb, DebugShapeVertex::ms_layout);
	m_ibh = bgfx::createIndexBuffer(ib);
	Init(bgfx::begin());
}

DebugDrawer::~DebugDrawer()
{
	bgfx::destroy(m_ibh);
	bgfx::destroy(m_vbh);
	for (uint32_t ii = 0; ii < Program::Count; ++ii)
	{
		bgfx::destroy(m_program[ii]);
	}
	bgfx::destroy(u_params);
	bgfx::destroy(s_texColor);
}

void DebugDrawer::Init(bgfx::Encoder* InEncoder)
{
	m_defaultEncoder = InEncoder;
}

void DebugDrawer::Begin(uint16_t InViewId, bool InDepthTestLess /*= true*/)
{
	BX_ASSERT(State::Count == m_state);

	m_viewId = InViewId;
	m_state = State::None;
	m_stack = 0;
	m_depthTestLess = InDepthTestLess;

	m_pos = 0;
	m_indexPos = 0;
	m_vertexPos = 0;
	m_posQuad = 0;


	Attrib& attrib = m_attrib[0];
	attrib.m_state = 0
		| BGFX_STATE_WRITE_RGB
		| (m_depthTestLess ? BGFX_STATE_DEPTH_TEST_LESS : BGFX_STATE_DEPTH_TEST_GREATER)
		| BGFX_STATE_CULL_CW
		| BGFX_STATE_WRITE_Z
		;
	attrib.m_scale = 1.0f;
	attrib.m_spin = 0.0f;
	attrib.m_offset = 0.0f;
	attrib.m_abgr = UINT32_MAX;
	attrib.m_stipple = false;
	attrib.m_wireframe = false;
	attrib.m_lod = 0;

	m_mtxStackCurrent = 0;
	m_mtxStack[m_mtxStackCurrent].reset();
}

void DebugDrawer::Push()
{
	BX_ASSERT(State::Count != m_state);
	++m_stack;
	m_attrib[m_stack] = m_attrib[m_stack - 1];
}

void DebugDrawer::Pop()
{
	BX_ASSERT(State::Count != m_state);
	const Attrib& curr = m_attrib[m_stack];
	const Attrib& prev = m_attrib[m_stack - 1];
	if (curr.m_stipple != prev.m_stipple
		|| curr.m_state != prev.m_state)
	{
		Flush();
	}
	--m_stack;
}

void DebugDrawer::Flush()
{
	if (m_pos != 0)
	{
		if (Moonlight::CheckAvailTransientBuffers(m_pos, DebugVertex::ms_layout, m_indexPos))
		{
			bgfx::TransientVertexBuffer tvb;
			bgfx::allocTransientVertexBuffer(&tvb, m_pos, DebugVertex::ms_layout);
			bx::memCopy(tvb.data, m_cache, m_pos * DebugVertex::ms_layout.m_stride);

			bgfx::TransientIndexBuffer tib;
			bgfx::allocTransientIndexBuffer(&tib, m_indexPos);
			bx::memCopy(tib.data, m_indices, m_indexPos * sizeof(uint16_t));

			const Attrib& attrib = m_attrib[m_stack];

			m_defaultEncoder->setVertexBuffer(0, &tvb);
			m_defaultEncoder->setIndexBuffer(&tib);
			m_defaultEncoder->setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_PT_LINES
				| attrib.m_state
				| BGFX_STATE_LINEAA
				| BGFX_STATE_BLEND_ALPHA
			);
			m_defaultEncoder->setTransform(m_mtxStack[m_mtxStackCurrent].mtx);
			bgfx::ProgramHandle program = m_program[attrib.m_stipple ? 1 : 0];
			m_defaultEncoder->submit(m_viewId, program);
		}

		m_state = State::None;
		m_pos = 0;
		m_indexPos = 0;
		m_vertexPos = 0;
	}
}

void DebugDrawer::SoftFlush()
{
	if (m_pos == uint16_t(BX_COUNTOF(m_cache)))
	{
		Flush();
	}
}

void DebugDrawer::End()
{
	BX_ASSERT(0 == m_stack, "Invalid stack %d.", m_stack);

	Flush();

	m_state = State::Count;
}

void DebugDrawer::Draw(const float* InOBB)
{
	//const Attrib& attrib = m_attrib[m_stack];
	//if (attrib.m_wireframe)
	{
		PushTransform(InOBB, 1);

		moveTo(-1.0f, -1.0f, -1.0f);
		lineTo(1.0f, -1.0f, -1.0f);
		lineTo(1.0f, 1.0f, -1.0f);
		lineTo(-1.0f, 1.0f, -1.0f);
		close();

		moveTo(-1.0f, 1.0f, 1.0f);
		lineTo(1.0f, 1.0f, 1.0f);
		lineTo(1.0f, -1.0f, 1.0f);
		lineTo(-1.0f, -1.0f, 1.0f);
		close();

		moveTo(1.0f, -1.0f, -1.0f);
		lineTo(1.0f, -1.0f, 1.0f);

		moveTo(1.0f, 1.0f, -1.0f);
		lineTo(1.0f, 1.0f, 1.0f);

		moveTo(-1.0f, 1.0f, -1.0f);
		lineTo(-1.0f, 1.0f, 1.0f);

		moveTo(-1.0f, -1.0f, -1.0f);
		lineTo(-1.0f, -1.0f, 1.0f);

		PopTransform();
	}
	//else
	{
		//Draw(DebugMesh::Cube, _obb.mtx, 1, false);
	}
}

void DebugDrawer::PushTransform(const void* _mtx, uint16_t _num, bool _flush /*= true*/)
{
	BX_ASSERT(m_mtxStackCurrent < BX_COUNTOF(m_mtxStack), "Out of matrix stack!");
	BX_ASSERT(State::Count != m_state);
	if (_flush)
	{
		Flush();
	}

	float* mtx = NULL;

	const MatrixStack& stack = m_mtxStack[m_mtxStackCurrent];

	if (NULL == stack.data)
	{
		mtx = (float*)_mtx;
	}
	else
	{
		mtx = (float*)alloca(_num * 64);
		for (uint16_t ii = 0; ii < _num; ++ii)
		{
			const float* mtxTransform = (const float*)_mtx;
			bx::mtxMul(&mtx[ii * 16], &mtxTransform[ii * 16], stack.data);
		}
	}

	m_mtxStackCurrent++;
	SetTransform(mtx, _num, _flush);
}

void DebugDrawer::PopTransform(bool _flush /*= true*/)
{
	BX_ASSERT(State::Count != m_state);
	if (_flush)
	{
		Flush();
	}

	m_mtxStackCurrent--;
}

void DebugDrawer::SetTransform(const void* _mtx, uint16_t _num /*= 1*/, bool _flush /*= true*/)
{
	BX_ASSERT(State::Count != m_state);
	if (_flush)
	{
		Flush();
	}

	MatrixStack& stack = m_mtxStack[m_mtxStackCurrent];

	if (NULL == _mtx)
	{
		stack.reset();
		return;
	}

	bgfx::Transform transform;
	stack.mtx = m_defaultEncoder->allocTransform(&transform, _num);
	stack.num = _num;
	stack.data = transform.data;
	bx::memCopy(transform.data, _mtx, _num * 64);
}

void DebugDrawer::moveTo(float _x, float _y, float _z /*= 0.0f*/)
{
	BX_ASSERT(State::Count != m_state);

	SoftFlush();

	m_state = State::MoveTo;

	DebugVertex& vertex = m_cache[m_pos];
	vertex.m_x = _x;
	vertex.m_y = _y;
	vertex.m_z = _z;

	Attrib& attrib = m_attrib[m_stack];
	vertex.m_abgr = attrib.m_abgr;
	vertex.m_len = attrib.m_offset;

	m_vertexPos = m_pos;
}

void DebugDrawer::lineTo(float _x, float _y, float _z /*= 0.0f*/)
{
	BX_ASSERT(State::Count != m_state);
	if (State::None == m_state)
	{
		moveTo(_x, _y, _z);
		return;
	}

	if (m_pos + 2 > uint16_t(BX_COUNTOF(m_cache)))
	{
		uint32_t pos = m_pos;
		uint32_t vertexPos = m_vertexPos;

		Flush();

		bx::memCopy(&m_cache[0], &m_cache[vertexPos], sizeof(DebugVertex));
		if (vertexPos == pos)
		{
			m_pos = 1;
		}
		else
		{
			bx::memCopy(&m_cache[1], &m_cache[pos - 1], sizeof(DebugVertex));
			m_pos = 2;
		}

		m_state = State::LineTo;
	}
	else if (State::MoveTo == m_state)
	{
		++m_pos;
		m_state = State::LineTo;
	}

	uint16_t prev = m_pos - 1;
	uint16_t curr = m_pos++;
	DebugVertex& vertex = m_cache[curr];
	vertex.m_x = _x;
	vertex.m_y = _y;
	vertex.m_z = _z;

	Attrib& attrib = m_attrib[m_stack];
	vertex.m_abgr = attrib.m_abgr;
	vertex.m_len = attrib.m_offset;

	float len = bx::length(bx::sub(bx::load<bx::Vec3>(&vertex.m_x), bx::load<bx::Vec3>(&m_cache[prev].m_x))) * attrib.m_scale;
	vertex.m_len = m_cache[prev].m_len + len;

	m_indices[m_indexPos++] = prev;
	m_indices[m_indexPos++] = curr;
}

void DebugDrawer::close()
{
	BX_ASSERT(State::Count != m_state);
	DebugVertex& vertex = m_cache[m_vertexPos];
	lineTo(vertex.m_x, vertex.m_y, vertex.m_z);

	m_state = State::None;
}

