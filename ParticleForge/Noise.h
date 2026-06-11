/*
	Noise.h

	Lightweight 3D value-noise used by the turbulence field of the particle
	simulation. Self-contained (only depends on the C++ standard library) so
	that the simulation engine stays independent of the After Effects SDK.
*/

#pragma once

#include <cstdint>
#include <cmath>

namespace pf {

// --- integer hashing ------------------------------------------------------

inline uint32_t HashU32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

inline uint32_t Hash3(int x, int y, int z)
{
	uint32_t h = HashU32(static_cast<uint32_t>(x) * 73856093U
					   ^ static_cast<uint32_t>(y) * 19349663U
					   ^ static_cast<uint32_t>(z) * 83492791U);
	return h;
}

inline double HashToUnit(uint32_t h)
{
	// 0..1
	return (h >> 8) * (1.0 / 16777216.0);
}

// --- smooth interpolation -------------------------------------------------

inline double Fade(double t)
{
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

inline double Lerp(double a, double b, double t)
{
	return a + (b - a) * t;
}

// 3D value noise, output range roughly 0..1
inline double ValueNoise3(double x, double y, double z)
{
	int xi = static_cast<int>(std::floor(x));
	int yi = static_cast<int>(std::floor(y));
	int zi = static_cast<int>(std::floor(z));

	double xf = x - xi;
	double yf = y - yi;
	double zf = z - zi;

	double u = Fade(xf);
	double v = Fade(yf);
	double w = Fade(zf);

	double c000 = HashToUnit(Hash3(xi,     yi,     zi));
	double c100 = HashToUnit(Hash3(xi + 1, yi,     zi));
	double c010 = HashToUnit(Hash3(xi,     yi + 1, zi));
	double c110 = HashToUnit(Hash3(xi + 1, yi + 1, zi));
	double c001 = HashToUnit(Hash3(xi,     yi,     zi + 1));
	double c101 = HashToUnit(Hash3(xi + 1, yi,     zi + 1));
	double c011 = HashToUnit(Hash3(xi,     yi + 1, zi + 1));
	double c111 = HashToUnit(Hash3(xi + 1, yi + 1, zi + 1));

	double x00 = Lerp(c000, c100, u);
	double x10 = Lerp(c010, c110, u);
	double x01 = Lerp(c001, c101, u);
	double x11 = Lerp(c011, c111, u);

	double y0 = Lerp(x00, x10, v);
	double y1 = Lerp(x01, x11, v);

	return Lerp(y0, y1, w);
}

// Fractal sum (a couple of octaves) - returns roughly -1..1
inline double FractalNoise3(double x, double y, double z)
{
	double sum = 0.0;
	double amp = 1.0;
	double norm = 0.0;
	for (int o = 0; o < 3; ++o) {
		sum  += (ValueNoise3(x, y, z) - 0.5) * 2.0 * amp;
		norm += amp;
		x *= 2.01; y *= 2.01; z *= 2.01;
		amp *= 0.5;
	}
	return sum / norm;
}

} // namespace pf
