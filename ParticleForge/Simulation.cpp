/*
	Simulation.cpp

	Implementation of the deterministic particle simulation declared in
	Simulation.h. See that header for an overview of the design.
*/

#include "Simulation.h"
#include "Noise.h"

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace pf {

namespace {

const double kPi = 3.14159265358979323846;

// --- per particle deterministic RNG --------------------------------------

struct Rng {
	uint32_t state;
	explicit Rng(uint32_t seed) : state(seed ? seed : 0x9e3779b9U) {}
	double next() {				// 0..1
		state = state * 1664525U + 1013904223U;
		return (state >> 8) * (1.0 / 16777216.0);
	}
	double signed1() {			// -1..1
		return next() * 2.0 - 1.0;
	}
};

inline double Clamp(double v, double lo, double hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// Evaluate a "... over life" curve for fraction f in [0,1].
double EvalCurve(double f, int mode)
{
	f = Clamp(f, 0.0, 1.0);
	switch (mode) {
		case kCurve_FadeIn:    return f;
		case kCurve_FadeOut:   return 1.0 - f;
		case kCurve_FadeInOut: return std::sin(f * kPi);
		case kCurve_Bell:      return 4.0 * f * (1.0 - f);
		case kCurve_Constant:
		default:               return 1.0;
	}
}

// Internal live particle state (camera/comp space).
struct Particle {
	double px, py, pz;
	double vx, vy, vz;
	double birthTime;
	double life;
	double baseSize;
	double r0, g0, b0;	// birth colour
	double r1, g1, b1;	// death colour
	double opacityScale;
	bool   alive;
};

// Produce an emitter-local spawn offset for the chosen emitter shape.
void EmitterOffset(const SimParams& p, Rng& rng, double& ox, double& oy, double& oz)
{
	switch (p.emitterType) {
		case kEmitter_Box:
			ox = rng.signed1() * 0.5 * p.emitterSizeX;
			oy = rng.signed1() * 0.5 * p.emitterSizeY;
			oz = rng.signed1() * 0.5 * p.emitterSizeZ;
			break;
		case kEmitter_Sphere: {
			// uniform direction, radius^(1/3) for even volume distribution
			double u  = rng.signed1();
			double th = rng.next() * 2.0 * kPi;
			double rr = std::pow(rng.next(), 1.0 / 3.0);
			double s  = std::sqrt(std::max(0.0, 1.0 - u * u));
			ox = std::cos(th) * s * rr * 0.5 * p.emitterSizeX;
			oy = std::sin(th) * s * rr * 0.5 * p.emitterSizeY;
			oz = u * rr * 0.5 * p.emitterSizeZ;
			break;
		}
		case kEmitter_Point:
		default:
			ox = oy = oz = 0.0;
			break;
	}
}

// Initial velocity vector based on direction, spread and randomness.
void InitialVelocity(const SimParams& p, Rng& rng, double& vx, double& vy, double& vz)
{
	// base direction in the XY plane, 0 rad points up (-Y)
	double bx = std::sin(p.direction);
	double by = -std::cos(p.direction);
	double bz = 0.0;

	// random unit vector for the spread cone
	double u  = rng.signed1();
	double th = rng.next() * 2.0 * kPi;
	double s  = std::sqrt(std::max(0.0, 1.0 - u * u));
	double rx = std::cos(th) * s;
	double ry = std::sin(th) * s;
	double rz = u;

	double sp = Clamp(p.directionSpread, 0.0, 1.0);
	double dx = bx + (rx - bx) * sp;
	double dy = by + (ry - by) * sp;
	double dz = bz + (rz - bz) * sp;

	double len = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (len < 1e-6) { dx = bx; dy = by; dz = bz; len = 1.0; }
	dx /= len; dy /= len; dz /= len;

	// speed with randomness; distribution biases toward lower speeds
	double rnd = rng.next();
	if (p.velocityDistribution > 0.0)
		rnd = std::pow(rnd, 1.0 + p.velocityDistribution * 2.0);
	double speed = p.velocity * (1.0 + (rnd * 2.0 - 1.0) * p.velocityRandom);

	vx = dx * speed;
	vy = dy * speed;
	vz = dz * speed;
}

} // namespace

void Simulate(const SimParams& p, std::vector<RParticle>& out)
{
	out.clear();

	if (p.currentTime <= 0.0 || p.frameRate <= 0.0)
		return;

	const double dt = 1.0 / p.frameRate;
	const int    nSteps = static_cast<int>(std::floor(p.currentTime / dt + 0.5));

	// Hard caps to keep pathological parameter values from hanging AE.
	const int    kMaxSteps  = 100000;
	const size_t kMaxAlive  = 400000;

	const int steps = std::min(nSteps, kMaxSteps);

	std::vector<Particle> live;
	live.reserve(1024);

	double spawnAccum = 0.0;
	uint32_t nextId = 0;

	for (int step = 0; step <= steps; ++step) {
		double t = step * dt;

		// --- spawn ----------------------------------------------------
		spawnAccum += p.particlesPerSec * dt;
		int toSpawn = static_cast<int>(spawnAccum);
		spawnAccum -= toSpawn;

		for (int i = 0; i < toSpawn && live.size() < kMaxAlive; ++i) {
			uint32_t id = nextId++;
			Rng rng(HashU32((id * 2654435761U) ^ ((p.randomSeed * 40503U) + 0x1234567U)));

			Particle pt;
			double ox, oy, oz;
			EmitterOffset(p, rng, ox, oy, oz);
			pt.px = p.posX + ox;
			pt.py = p.posY + oy;
			pt.pz = p.posZ + oz;

			InitialVelocity(p, rng, pt.vx, pt.vy, pt.vz);

			pt.birthTime = t;
			pt.life      = std::max(0.01, p.life * (1.0 + rng.signed1() * p.lifeRandom));
			pt.baseSize  = std::max(0.0, p.size * (1.0 + rng.signed1() * p.sizeRandom));

			pt.r0 = p.colorBirth[0]; pt.g0 = p.colorBirth[1]; pt.b0 = p.colorBirth[2];
			pt.r1 = p.colorDeath[0]; pt.g1 = p.colorDeath[1]; pt.b1 = p.colorDeath[2];
			pt.opacityScale = p.opacity;
			pt.alive = true;

			live.push_back(pt);
		}

		// Avoid integrating one extra step past the final time.
		if (step == steps)
			break;

		// --- integrate ------------------------------------------------
		const double spinRad = p.spin * kPi / 180.0;	// rad/sec
		const double airFactor = std::max(0.0, 1.0 - p.airResistance * dt);

		for (Particle& pt : live) {
			if (!pt.alive) continue;
			double age = t - pt.birthTime;
			if (age > pt.life) { pt.alive = false; continue; }

			// gravity + wind
			pt.vy += p.gravity * dt;
			pt.vx += p.windX * dt;
			pt.vy += p.windY * dt;

			// turbulence field (curl-free pseudo turbulence)
			if (p.turbAmount != 0.0) {
				double nx = FractalNoise3(pt.px * p.turbScale,
										  pt.py * p.turbScale,
										  pt.pz * p.turbScale + p.turbEvolution);
				double ny = FractalNoise3(pt.px * p.turbScale + 71.3,
										  pt.py * p.turbScale - 12.7,
										  pt.pz * p.turbScale + p.turbEvolution + 4.2);
				double nz = FractalNoise3(pt.px * p.turbScale - 33.1,
										  pt.py * p.turbScale + 91.5,
										  pt.pz * p.turbScale + p.turbEvolution - 8.9);
				pt.vx += nx * p.turbAmount * dt;
				pt.vy += ny * p.turbAmount * dt;
				pt.vz += nz * p.turbAmount * dt;
			}

			// spin around the emitter centre (Z axis)
			if (spinRad != 0.0) {
				double rx = pt.px - p.posX;
				double ry = pt.py - p.posY;
				// tangential acceleration = omega x r
				pt.vx += -ry * spinRad * dt;
				pt.vy +=  rx * spinRad * dt;
			}

			// air resistance
			pt.vx *= airFactor;
			pt.vy *= airFactor;
			pt.vz *= airFactor;

			// integrate position
			pt.px += pt.vx * dt;
			pt.py += pt.vy * dt;
			pt.pz += pt.vz * dt;
		}

		// periodic compaction of dead particles
		if ((step & 31) == 0) {
			live.erase(std::remove_if(live.begin(), live.end(),
						[](const Particle& q){ return !q.alive; }),
					   live.end());
		}
	}

	// --- project alive particles to screen at currentTime ----------------
	const double tNow = steps * dt;
	out.reserve(live.size());

	for (const Particle& pt : live) {
		if (!pt.alive) continue;
		double age = tNow - pt.birthTime;
		if (age < 0.0 || age > pt.life) continue;
		double f = age / pt.life;

		double sizeMul = EvalCurve(f, p.sizeOverLife);
		// "Constant" size curve should keep full size, not 1.0 multiplier only
		double sizePx  = pt.baseSize * sizeMul;
		if (sizePx <= 0.05) continue;

		double opa = pt.opacityScale * EvalCurve(f, p.opacityOverLife);
		if (opa <= 0.0) continue;

		// perspective projection around the optical centre
		double denom = p.focalLength + pt.pz;
		if (denom <= 1.0) continue;	// behind / at the camera
		double scale = p.focalLength / denom;

		RParticle rp;
		rp.x = p.centerX + (pt.px - p.centerX) * scale;
		rp.y = p.centerY + (pt.py - p.centerY) * scale;
		rp.radius = sizePx * scale * 0.5;	// size is diameter-ish
		rp.r = pt.r0 + (pt.r1 - pt.r0) * f;
		rp.g = pt.g0 + (pt.g1 - pt.g0) * f;
		rp.b = pt.b0 + (pt.b1 - pt.b0) * f;
		rp.a = Clamp(opa, 0.0, 1.0);
		rp.type = p.particleType;
		rp.depth = pt.pz;

		if (rp.radius < 0.25) continue;
		out.push_back(rp);
	}

	// Back-to-front so nearer particles draw on top (matters for Normal blend).
	std::sort(out.begin(), out.end(),
			  [](const RParticle& a, const RParticle& b){ return a.depth > b.depth; });
}

} // namespace pf
