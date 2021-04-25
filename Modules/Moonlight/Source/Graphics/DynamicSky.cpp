#include "DynamicSky.h"
#include "ShaderStructures.h"
#include <memory>
#include <imgui.h>
#include <bx/timer.h>

namespace Moonlight
{
	// HDTV rec. 709 matrix.
	static float M_XYZ2RGB[] =
	{
		 3.240479f, -0.969256f,  0.055648f,
		-1.53715f,   1.875991f, -0.204043f,
		-0.49853f,   0.041556f,  1.057311f,
	};

	// Converts color repesentation from CIE XYZ to RGB color-space.
	Color xyzToRgb(const Color& xyz)
	{
		Color rgb;
		rgb.x = M_XYZ2RGB[0] * xyz.x + M_XYZ2RGB[3] * xyz.y + M_XYZ2RGB[6] * xyz.z;
		rgb.y = M_XYZ2RGB[1] * xyz.x + M_XYZ2RGB[4] * xyz.y + M_XYZ2RGB[7] * xyz.z;
		rgb.z = M_XYZ2RGB[2] * xyz.x + M_XYZ2RGB[5] * xyz.y + M_XYZ2RGB[8] * xyz.z;
		return rgb;
	};


	// Precomputed luminance of sunlight in XYZ colorspace.
	// Computed using code from Game Engine Gems, Volume One, chapter 15. Implementation based on Dr. Richard Bird model.
	// This table is used for piecewise linear interpolation. Transitions from and to 0.0 at sunset and sunrise are highly inaccurate
	static std::map<float, Color> sunLuminanceXYZTable = {
		{  5.0f, {  0.000000f,  0.000000f,  0.000000f } },
		{  7.0f, { 12.703322f, 12.989393f,  9.100411f } },
		{  8.0f, { 13.202644f, 13.597814f, 11.524929f } },
		{  9.0f, { 13.192974f, 13.597458f, 12.264488f } },
		{ 10.0f, { 13.132943f, 13.535914f, 12.560032f } },
		{ 11.0f, { 13.088722f, 13.489535f, 12.692996f } },
		{ 12.0f, { 13.067827f, 13.467483f, 12.745179f } },
		{ 13.0f, { 13.069653f, 13.469413f, 12.740822f } },
		{ 14.0f, { 13.094319f, 13.495428f, 12.678066f } },
		{ 15.0f, { 13.142133f, 13.545483f, 12.526785f } },
		{ 16.0f, { 13.201734f, 13.606017f, 12.188001f } },
		{ 17.0f, { 13.182774f, 13.572725f, 11.311157f } },
		{ 18.0f, { 12.448635f, 12.672520f,  8.267771f } },
		{ 20.0f, {  0.000000f,  0.000000f,  0.000000f } },
	};


	// Precomputed luminance of sky in the zenith point in XYZ colorspace.
	// Computed using code from Game Engine Gems, Volume One, chapter 15. Implementation based on Dr. Richard Bird model.
	// This table is used for piecewise linear interpolation. Day/night transitions are highly inaccurate.
	// The scale of luminance change in Day/night transitions is not preserved.
	// Luminance at night was increased to eliminate need the of HDR render.
	static std::map<float, Color> skyLuminanceXYZTable = {
		{  0.0f, { 0.308f,    0.308f,    0.411f    } },
		{  1.0f, { 0.308f,    0.308f,    0.410f    } },
		{  2.0f, { 0.301f,    0.301f,    0.402f    } },
		{  3.0f, { 0.287f,    0.287f,    0.382f    } },
		{  4.0f, { 0.258f,    0.258f,    0.344f    } },
		{  5.0f, { 0.258f,    0.258f,    0.344f    } },
		{  7.0f, { 0.962851f, 1.000000f, 1.747835f } },
		{  8.0f, { 0.967787f, 1.000000f, 1.776762f } },
		{  9.0f, { 0.970173f, 1.000000f, 1.788413f } },
		{ 10.0f, { 0.971431f, 1.000000f, 1.794102f } },
		{ 11.0f, { 0.972099f, 1.000000f, 1.797096f } },
		{ 12.0f, { 0.972385f, 1.000000f, 1.798389f } },
		{ 13.0f, { 0.972361f, 1.000000f, 1.798278f } },
		{ 14.0f, { 0.972020f, 1.000000f, 1.796740f } },
		{ 15.0f, { 0.971275f, 1.000000f, 1.793407f } },
		{ 16.0f, { 0.969885f, 1.000000f, 1.787078f } },
		{ 17.0f, { 0.967216f, 1.000000f, 1.773758f } },
		{ 18.0f, { 0.961668f, 1.000000f, 1.739891f } },
		{ 20.0f, { 0.264f,    0.264f,    0.352f    } },
		{ 21.0f, { 0.264f,    0.264f,    0.352f    } },
		{ 22.0f, { 0.290f,    0.290f,    0.386f    } },
		{ 23.0f, { 0.303f,    0.303f,    0.404f    } },
	};


	// Turbidity tables. Taken from:
	// A. J. Preetham, P. Shirley, and B. Smits. A Practical Analytic Model for Daylight. SIGGRAPH '99
	// Coefficients correspond to xyY colorspace.
	static Color ABCDE[] =
	{
		{ -0.2592f, -0.2608f, -1.4630f },
		{  0.0008f,  0.0092f,  0.4275f },
		{  0.2125f,  0.2102f,  5.3251f },
		{ -0.8989f, -1.6537f, -2.5771f },
		{  0.0452f,  0.0529f,  0.3703f },
	};
	static Color ABCDE_t[] =
	{
		{ -0.0193f, -0.0167f,  0.1787f },
		{ -0.0665f, -0.0950f, -0.3554f },
		{ -0.0004f, -0.0079f, -0.0227f },
		{ -0.0641f, -0.0441f,  0.1206f },
		{ -0.0033f, -0.0109f, -0.0670f },
	};

	DynamicSky::DynamicSky(uint32_t inWidth, uint32_t inHeight)
	{
		ScreenPosVertex::init();

		m_sky = ShaderCommand("Assets/Shaders/Sky/Sky");

		ScreenPosVertex* skyVerts = (ScreenPosVertex*)malloc(inWidth * inHeight * sizeof(ScreenPosVertex));

		for (uint32_t i = 0; i < inHeight; ++i)
		{
			for (uint32_t j = 0; j < inWidth; ++j)
			{
				ScreenPosVertex& vert = skyVerts[i * inHeight + j];
				vert.Position.x = float(j) / (inWidth - 1) * 2.f - 1.f;
				vert.Position.y = float(i) / (inHeight - 1) * 2.f - 1.f;
			}
		}

		uint16_t* skyIndicies = (uint16_t*)malloc((inHeight - 1) * (inWidth - 1) * 6 * sizeof(uint16_t));

		int k = 0;
		for (uint32_t i = 0; i < inHeight - 1; i++)
		{
			for (uint32_t j = 0; j < inWidth - 1; ++j)
			{
				skyIndicies[k++] = (uint16_t)(j + 0 + inWidth * (i + 0));
				skyIndicies[k++] = (uint16_t)(j + 1 + inWidth * (i + 0));
				skyIndicies[k++] = (uint16_t)(j + 0 + inWidth * (i + 1));

				skyIndicies[k++] = (uint16_t)(j + 1 + inWidth * (i + 0));
				skyIndicies[k++] = (uint16_t)(j + 1 + inWidth * (i + 1));
				skyIndicies[k++] = (uint16_t)(j + 0 + inWidth * (i + 1));
			}
		}

		m_vbh = bgfx::createVertexBuffer(bgfx::copy(skyVerts, sizeof(ScreenPosVertex) * inHeight * inWidth), ScreenPosVertex::ms_layout);
		m_ibh = bgfx::createIndexBuffer(bgfx::copy(skyIndicies, sizeof(uint16_t) * k));

		m_sunLuminanceXYZ.SetMap(sunLuminanceXYZTable);
		m_skyLuminanceXYZ.SetMap(skyLuminanceXYZTable);

		m_timeOffset = bx::getHPCounter();
		m_time = 0.0f;
		m_timeScale = 1.0f;

		u_sunLuminance = bgfx::createUniform("u_sunLuminance", bgfx::UniformType::Vec4);
		u_skyLuminanceXYZ = bgfx::createUniform("u_skyLuminanceXYZ", bgfx::UniformType::Vec4);
		u_skyLuminance = bgfx::createUniform("u_skyLuminance", bgfx::UniformType::Vec4);
		u_sunDirection = bgfx::createUniform("u_sunDirection", bgfx::UniformType::Vec4);
		u_parameters = bgfx::createUniform("u_parameters", bgfx::UniformType::Vec4);
		u_perezCoeff = bgfx::createUniform("u_perezCoeff", bgfx::UniformType::Vec4, 5);
		m_turbidity = 2.15f;
	}

	void DynamicSky::DrawImGui()
	{
		if (ImGui::Begin("ProceduralSky"))
		{
			ImGui::SliderFloat("Time scale", &m_timeScale, 0.0f, 1.0f);
			ImGui::SliderFloat("Time", &m_time, 0.0f, 24.0f);
			ImGui::SliderFloat("Latitude", &m_sun.m_latitude, -90.0f, 90.0f);
			ImGui::SliderFloat("Turbidity", &m_turbidity, 1.9f, 10.0f);

			const char* items[] =
			{
				"January",
				"February",
				"March",
				"April",
				"May",
				"June",
				"July",
				"August",
				"September",
				"October",
				"November",
				"December"
			};

			ImGui::Combo("Month", (int*)&m_sun.m_month, items, 12);

			ImGui::End();
		}
	}

	void DynamicSky::Draw(uint32_t inViewId)
	{
		int64_t now = bx::getHPCounter();
		static int64_t last = now;
		const int64_t frameTime = now - last;
		last = now;
		const double freq = double(bx::getHPFrequency());
		const float deltaTime = float(frameTime / freq);
		m_time += m_timeScale * deltaTime;
		m_time = bx::mod(m_time, 24.0f);
		m_sun.Update(m_time);

		Color sunLuminanceXYZ = m_sunLuminanceXYZ.GetValue(m_time);
		Color sunLuminanceRGB = xyzToRgb(sunLuminanceXYZ);

		Color skyLuminanceXYZ = m_skyLuminanceXYZ.GetValue(m_time);
		Color skyLuminanceRGB = xyzToRgb(skyLuminanceXYZ);

		bgfx::setUniform(u_sunLuminance, &sunLuminanceRGB.x);
		bgfx::setUniform(u_skyLuminanceXYZ, &skyLuminanceXYZ.x);
		bgfx::setUniform(u_skyLuminance, &skyLuminanceRGB.x);

		bgfx::setUniform(u_sunDirection, &m_sun.m_sunDir.x);

		float exposition[4] = { 0.02f, 3.0f, 0.1f, m_time };
		bgfx::setUniform(u_parameters, exposition);

		float perezCoeff[4 * 5];
		computePerezCoeff(m_turbidity, perezCoeff);
		bgfx::setUniform(u_perezCoeff, perezCoeff, 5);

		bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_EQUAL);
		bgfx::setIndexBuffer(m_ibh);
		bgfx::setVertexBuffer(0, m_vbh);
		bgfx::submit(inViewId, m_sky.GetProgram());
	}

	void DynamicSky::computePerezCoeff(float _turbidity, float* _outPerezCoeff)
	{
		const bx::Vec3 turbidity = { _turbidity, _turbidity, _turbidity };
		for (uint32_t ii = 0; ii < 5; ++ii)
		{
			const bx::Vec3 tmp = bx::mad(ABCDE_t[ii], turbidity, ABCDE[ii]);
			float* out = _outPerezCoeff + 4 * ii;
			bx::store(out, tmp);
			out[3] = 0.0f;
		}
	}

	SunController::SunController() : m_latitude(50.0f)
		, m_month(June)
		, m_eclipticObliquity(bx::toRad(23.4f))
		, m_delta(0.0f)
	{
		m_northDir = { 1.0f,  0.0f, 0.0f };
		m_sunDir = { 0.0f, -1.0f, 0.0f };
		m_upDir = { 0.0f,  1.0f, 0.0f };
	}

	void SunController::Update(float _time)
	{
		CalculateSunOrbit();
		UpdateSunPosition(_time - 12.0f);
	}

	void SunController::CalculateSunOrbit()
	{
		float day = 30.0f * m_month + 15.0f;
		float lambda = 280.46f + 0.9856474f * day;
		lambda = bx::toRad(lambda);
		m_delta = bx::asin(bx::sin(m_eclipticObliquity) * bx::sin(lambda));
	}

	void SunController::UpdateSunPosition(float _hour)
	{
		const float latitude = bx::toRad(m_latitude);
		const float hh = _hour * bx::kPi / 12.0f;
		const float azimuth = bx::atan2(
			bx::sin(hh)
			, bx::cos(hh) * bx::sin(latitude) - bx::tan(m_delta) * bx::cos(latitude)
		);

		const float altitude = bx::asin(
			bx::sin(latitude) * bx::sin(m_delta) + bx::cos(latitude) * bx::cos(m_delta) * bx::cos(hh)
		);

		const bx::Quaternion rot0 = bx::rotateAxis(m_upDir, -azimuth);
		const bx::Vec3 dir = bx::mul(m_northDir, rot0);
		const bx::Vec3 uxd = bx::cross(m_upDir, dir);

		const bx::Quaternion rot1 = bx::rotateAxis(uxd, altitude);
		m_sunDir = bx::mul(dir, rot1);
	}

}