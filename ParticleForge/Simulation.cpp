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
	uint32_t id;		// stable id for deterministic trail seeding
	double trailAccum;	// fractional trail-spawn accumulator
	uint32_t trailCount;// number of trail children emitted so far
	bool   alive;
};

// Trail (aux) child particle. Children never spawn children of their own and
// carry a single constant colour (sampled from the parent at emission time).
struct Child {
	double px, py, pz;
	double vx, vy, vz;
	double birthTime;
	double life;
	double baseSize;
	double r, g, b;
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

// Decide which source time (in seconds) a Texture particle should sample,
// given its stable id and current age. Returns a negative sentinel for the
// "Still" mode, which the AE glue maps to the current comp frame. The random
// modes hash the particle id + seed so the choice is stable under scrubbing,
// matching the deterministic scheme used elsewhere in the simulation.
double TexSampleTime(const SimParams& p, uint32_t id, double age)
{
	const double loop = p.texLoopDur;
	const double e    = age > 0.0 ? age : 0.0;
	switch (p.texTimeSampling) {
		case kTexTime_BirthLoop:
			return (loop > 0.0) ? std::fmod(e, loop) : e;
		case kTexTime_RandomLoop: {
			double off = HashToUnit(HashU32((id * 2654435761U) ^
								(p.randomSeed * 2246822519U + 0x517CC1B7U)))
						 * (loop > 0.0 ? loop : 1.0);
			double s = off + e;
			return (loop > 0.0) ? std::fmod(s, loop) : s;
		}
		case kTexTime_RandomStill: {
			double fr = HashToUnit(HashU32((id * 40503U) ^
								(p.randomSeed * 668265263U + 0x27D4EB2FU)));
			return fr * (loop > 0.0 ? loop : 0.0);
		}
		case kTexTime_Still:
		default:
			return -1.0;	// sentinel: use the current comp frame
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

	std::vector<Child> trail;
	const bool trailOn = (p.trailEnable != 0) &&
						 (p.trailParticlesPerSec > 0.0) && (p.trailLife > 0.0);

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
			pt.id         = id;
			pt.trailAccum = 0.0;
			pt.trailCount = 0;
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

			// --- shed trail (aux) children along the parent's path --------
			if (trailOn) {
				pt.trailAccum += p.trailParticlesPerSec * dt;
				int nChild = static_cast<int>(pt.trailAccum);
				pt.trailAccum -= nChild;

				double fpar = pt.life > 0.0 ? (age / pt.life) : 0.0;
				double cr = pt.r0 + (pt.r1 - pt.r0) * fpar;
				double cg = pt.g0 + (pt.g1 - pt.g0) * fpar;
				double cb = pt.b0 + (pt.b1 - pt.b0) * fpar;

				for (int k = 0; k < nChild && trail.size() < kMaxAlive; ++k) {
					Rng crng(HashU32((pt.id * 2246822519U) ^
									 ((pt.trailCount++ + 1U) * 3266489917U) ^
									 (p.randomSeed * 668265263U)));

					Child c;
					c.px = pt.px; c.py = pt.py; c.pz = pt.pz;
					// inherit a fraction of parent velocity plus a little jitter
					double jitter = p.velocity * 0.04;
					c.vx = pt.vx * p.trailInheritVel + crng.signed1() * jitter;
					c.vy = pt.vy * p.trailInheritVel + crng.signed1() * jitter;
					c.vz = pt.vz * p.trailInheritVel + crng.signed1() * jitter;
					c.birthTime    = t;
					c.life         = p.trailLife * (1.0 + crng.signed1() * 0.2);
					c.baseSize     = p.trailSize;
					c.r = cr; c.g = cg; c.b = cb;
					c.opacityScale = p.trailOpacity;
					c.alive        = true;
					trail.push_back(c);
				}
			}

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

		// --- integrate trail children (same forces, no spin, no spawning) -
		for (Child& c : trail) {
			if (!c.alive) continue;
			double cage = t - c.birthTime;
			if (cage > c.life) { c.alive = false; continue; }

			c.vy += p.gravity * dt;
			c.vx += p.windX * dt;
			c.vy += p.windY * dt;

			if (p.turbAmount != 0.0) {
				double nx = FractalNoise3(c.px * p.turbScale,
										  c.py * p.turbScale,
										  c.pz * p.turbScale + p.turbEvolution);
				double ny = FractalNoise3(c.px * p.turbScale + 71.3,
										  c.py * p.turbScale - 12.7,
										  c.pz * p.turbScale + p.turbEvolution + 4.2);
				double nz = FractalNoise3(c.px * p.turbScale - 33.1,
										  c.py * p.turbScale + 91.5,
										  c.pz * p.turbScale + p.turbEvolution - 8.9);
				c.vx += nx * p.turbAmount * dt;
				c.vy += ny * p.turbAmount * dt;
				c.vz += nz * p.turbAmount * dt;
			}

			c.vx *= airFactor;
			c.vy *= airFactor;
			c.vz *= airFactor;

			c.px += c.vx * dt;
			c.py += c.vy * dt;
			c.pz += c.vz * dt;
		}

		// periodic compaction of dead particles
		if ((step & 31) == 0) {
			live.erase(std::remove_if(live.begin(), live.end(),
						[](const Particle& q){ return !q.alive; }),
					   live.end());
			if (trailOn)
				trail.erase(std::remove_if(trail.begin(), trail.end(),
							[](const Child& q){ return !q.alive; }),
						   trail.end());
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
		rp.texSampleTime = TexSampleTime(p, pt.id, age);

		if (rp.radius < 0.25) continue;
		out.push_back(rp);
	}

	// --- project alive trail children (rendered as soft glow spheres) -----
	for (const Child& c : trail) {
		if (!c.alive) continue;
		double cage = tNow - c.birthTime;
		if (cage < 0.0 || cage > c.life) continue;
		double f = cage / c.life;

		if (c.baseSize <= 0.05) continue;

		// children fade out over their life
		double opa = c.opacityScale * EvalCurve(f, kCurve_FadeOut);
		if (opa <= 0.0) continue;

		double denom = p.focalLength + c.pz;
		if (denom <= 1.0) continue;
		double scale = p.focalLength / denom;

		RParticle rp;
		rp.x = p.centerX + (c.px - p.centerX) * scale;
		rp.y = p.centerY + (c.py - p.centerY) * scale;
		rp.radius = c.baseSize * scale * 0.5;
		rp.r = c.r; rp.g = c.g; rp.b = c.b;
		rp.a = Clamp(opa, 0.0, 1.0);
		rp.type = kParticle_GlowSphere;
		rp.depth = c.pz;
		rp.texSampleTime = -1.0;

		if (rp.radius < 0.25) continue;
		out.push_back(rp);
	}

	// Back-to-front so nearer particles draw on top (matters for Normal blend).
	std::sort(out.begin(), out.end(),
			  [](const RParticle& a, const RParticle& b){ return a.depth > b.depth; });
}

} // namespace pf
