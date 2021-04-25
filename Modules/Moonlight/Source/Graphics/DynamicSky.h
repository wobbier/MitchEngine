#pragma once
#include "ShaderCommand.h"
#include "bx/math.h"
#include <map>
#include <Materials/DynamicSkyMaterial.h>

namespace Moonlight
{
	typedef bx::Vec3 Color;

	// Performs piecewise linear interpolation of a Color parameter.
	class DynamicValueController
	{
		typedef Color ValueType;
		typedef std::map<float, ValueType> KeyMap;

	public:
		DynamicValueController()
		{
		}

		~DynamicValueController()
		{
		}

		void SetMap(const KeyMap& keymap)
		{
			m_keyMap = keymap;
		}

		ValueType GetValue(float time) const
		{
			typename KeyMap::const_iterator itUpper = m_keyMap.upper_bound(time + 1e-6f);
			typename KeyMap::const_iterator itLower = itUpper;
			--itLower;

			if (itLower == m_keyMap.end())
			{
				return itUpper->second;
			}

			if (itUpper == m_keyMap.end())
			{
				return itLower->second;
			}

			float lowerTime = itLower->first;
			const ValueType& lowerVal = itLower->second;
			float upperTime = itUpper->first;
			const ValueType& upperVal = itUpper->second;

			if (lowerTime == upperTime)
			{
				return lowerVal;
			}

			return interpolate(lowerTime, lowerVal, upperTime, upperVal, time);
		};

		void Clear()
		{
			m_keyMap.clear();
		};

	private:
		ValueType interpolate(float lowerTime, const ValueType& lowerVal, float upperTime, const ValueType& upperVal, float time) const
		{
			const float tt = (time - lowerTime) / (upperTime - lowerTime);
			const ValueType result = bx::lerp(lowerVal, upperVal, tt);
			return result;
		};

		KeyMap	m_keyMap;
	};
	class SunController
	{
	public:
		enum Month : int
		{
			January = 0,
			February,
			March,
			April,
			May,
			June,
			July,
			August,
			September,
			October,
			November,
			December
		};

		SunController();

		void Update(float _time);

		bx::Vec3 m_northDir;
		bx::Vec3 m_sunDir;
		bx::Vec3 m_upDir;
		float m_latitude;
		Month m_month;

	private:
		void CalculateSunOrbit();

		void UpdateSunPosition(float _hour);

		float m_eclipticObliquity;
		float m_delta;
	};

	class DynamicSky
	{
	public:
		DynamicSky(uint32_t inWidth, uint32_t inHeight);

		void DrawImGui();
		void Draw(uint32_t inViewId);

		void computePerezCoeff(float _turbidity, float* _outPerezCoeff);
		bgfx::VertexBufferHandle m_vbh;
		bgfx::IndexBufferHandle m_ibh;

		// Shader stuff
		SunController m_sun;

		DynamicValueController m_sunLuminanceXYZ;
		DynamicValueController m_skyLuminanceXYZ;

		float m_time;
		float m_timeScale;
		int64_t m_timeOffset;
		UniquePtr<DynamicSkyMaterial> m_material;

		float m_turbidity;
	};
}