#pragma once

#include <math.h>

namespace SilverLining
{
	template<class T>
	class MathUtilsTemplate
	{
	public:
		static T smoothstep(T edge0, T edge1, T x);
		static T clamp(T x, T low, T high);

		static T Pi(void);
		static T TwoPi(void);
		static T PiByTwo(void);

		static T ToRadians(T degrees);
		static T ToDegrees(T radians);

		static T Epsilon();

		static T Abs(T val);

		/** Unsafe acos*/
		static T Acos(T val);

		/** A safe version of arc-cosine that protects from precision errors on the parameter. */
		static T SafeAcos(T val);
	};

	template<class T>
	T MathUtilsTemplate<T>::smoothstep(T edge0, T edge1, T x)
	{
		// Scale, bias and saturate x to 0..1 range
		x = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
		// Evaluate polynomial
		return x*x*(3 - 2 * x);
	}

	template<class T>
	T MathUtilsTemplate<T>::clamp(T x, T low, T high)
	{
		if (x < low) return low;
		if (x > high) return high;
		return x;
	}

	template<class T>
	T MathUtilsTemplate<T>::Pi(void)
	{
		return (T)3.14159265;
	}

	template<class T>
	T MathUtilsTemplate<T>::TwoPi(void)
	{
		return Pi() * 2.0;
	}

	template<class T>
	T MathUtilsTemplate<T>::PiByTwo(void)
	{
		return Pi() * 0.5;
	}

	template<class T>
	T MathUtilsTemplate<T>::ToRadians(T degrees)
	{
		return degrees*(Pi() / (T)180.0);
	}

	template<class T>
	T MathUtilsTemplate<T>::ToDegrees(T radians)
	{
		return radians*((T)180.0 / Pi());
	}

	template<class T>
	T MathUtilsTemplate<T>::Epsilon()
	{
		return 1E-6f;
	}

	template<class T>
	T MathUtilsTemplate<T>::Abs(T val)
	{
		return  (val < 0)? -val : val;
	}

	template<class T>
	T MathUtilsTemplate<T>::SafeAcos(T x)
	{
		return ((-1.0 < x) ? ((x < +1.0) ? acos(x) : acos(+1.0)) : acos(-1.0));
	}

	template<class T>
	T MathUtilsTemplate<T>::Acos(T val)
	{
		return acos(val);
	}

	typedef MathUtilsTemplate<double> MathUtils;
	typedef MathUtilsTemplate<float> MathUtilsFloat;

}