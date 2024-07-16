#pragma once
#include <cmath>

namespace dae
{
	/* --- HELPER STRUCTS --- */
	struct Int2
	{
		int x{};
		int y{};
	};

	//
	constexpr auto PI = 3.14159265358979323846f;
	constexpr auto PI_DIV_2 = 1.57079632679489661923f;
	constexpr auto PI_DIV_4 = 0.785398163397448309616f;
	constexpr auto PI_2 = 6.283185307179586476925f;
	constexpr auto PI_4 = 12.56637061435917295385f;

	constexpr auto TO_DEGREES = (180.0f / PI);
	constexpr auto TO_RADIANS(PI / 180.0f);

	/* --- HELPER FUNCTIONS --- */
	inline float Power2(float a)
	{
		return a * a;
	}

	inline float Interpolate(float a, float b, float factor)
	{
		return ((1 - factor) * a) + (factor * b);
	}

	inline int Limit(const int v, int min, int max)
	{
		return v < min ? min : (v > max ? max : v);
	}

	inline float Limit(const float v, float min, float max)
	{
		return v < min ? min : (v > max ? max : v);
	}

	inline float Constrain(const float v)
	{
		if (v < 0.f) return 0.f;
		if (v > 1.f) return 1.f;
		return v;
	}

	inline bool isEqual(float a, float b, float epsilon = FLT_EPSILON)
	{
		return abs(a - b) < epsilon;
	}

	inline float Remap(float value, float min, float max)
	{
		const float clamped{ std::clamp(value, min, max) };
		return (clamped - min) / (max - min);
	}
}