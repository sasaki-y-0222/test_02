/*
	Simulation.h

	Pure C++ particle simulation engine for the ParticleForge After Effects
	plug-in. Deliberately free of any After Effects SDK dependency so that it
	can be unit tested and reasoned about in isolation.

	The simulation is fully deterministic: given identical SimParams it always
	produces identical output. The plug-in re-simulates from t = 0 up to the
	current composition time on every rendered frame (the same approach used by
	classic temporal particle systems). This keeps the effect stateless from
	After Effects' point of view while still producing continuous motion.
*/

#pragma once

#include <vector>

namespace pf {

// Emitter shapes
enum EmitterType {
	kEmitter_Point  = 1,
	kEmitter_Box    = 2,
	kEmitter_Sphere = 3
};

// Particle render primitives
enum ParticleType {
	kParticle_Sphere     = 1,
	kParticle_GlowSphere = 2,
	kParticle_Star       = 3,
	kParticle_Texture    = 4	// sprite sampled from a host layer (AE glue);
								// falls back to GlowSphere when no layer is set
};

// Curve shapes used by "... over Life" controls
enum LifeCurve {
	kCurve_Constant    = 1,
	kCurve_FadeIn      = 2,	// 0 -> 1
	kCurve_FadeOut     = 3,	// 1 -> 0
	kCurve_FadeInOut   = 4,	// 0 -> 1 -> 0 (smooth)
	kCurve_Bell        = 5	// fast in / fast out
};

enum BlendMode {
	kBlend_Add    = 1,
	kBlend_Normal = 2,
	kBlend_Screen = 3
};

// How a Texture particle samples its source layer over time. The simulation
// only decides *which source time* each particle wants (in seconds); the AE
// glue is responsible for actually checking the layer out at that time.
enum TexTimeSampling {
	kTexTime_Still       = 1,	// always the current comp frame (legacy behaviour)
	kTexTime_BirthLoop   = 2,	// play from the particle's birth, looping at the end
	kTexTime_RandomLoop  = 3,	// per-particle random start frame, looping
	kTexTime_RandomStill = 4	// per-particle random single frame, frozen
};

struct SimParams {
	// Emitter
	double  particlesPerSec;
	int     emitterType;
	double  posX, posY, posZ;			// emitter origin (output-pixel space, Z relative)
	double  direction;					// radians, 0 points up (-Y)
	double  directionSpread;			// 0..1
	double  velocity;					// px/sec
	double  velocityRandom;				// 0..1
	double  velocityDistribution;		// 0..1
	double  emitterSizeX, emitterSizeY, emitterSizeZ;

	// Particle
	double  life;						// seconds
	double  lifeRandom;					// 0..1
	int     particleType;
	int     texTimeSampling;			// TexTimeSampling (Texture particles only)
	double  texLoopDur;					// seconds; source length used for looping (0 = none)
	double  size;						// px
	double  sizeRandom;					// 0..1
	int     sizeOverLife;				// LifeCurve
	double  rotation;					// initial sprite angle, degrees
	double  rotationRandom;				// 0..1 randomises the initial angle
	double  rotationSpeed;				// deg/sec (may be negative)
	double  rotationSpeedRandom;		// 0..1 randomises the spin rate
	double  opacity;					// 0..1
	int     opacityOverLife;			// LifeCurve
	double  colorBirth[3];				// 0..1 rgb
	double  colorDeath[3];				// 0..1 rgb
	int     blendMode;

	// Physics
	double  gravity;					// px/sec^2 (positive = down)
	double  airResistance;				// 1/sec
	double  windX, windY;				// px/sec^2
	double  spin;						// deg/sec around emitter (Z axis)
	double  turbAmount;					// px/sec^2
	double  turbScale;					// spatial frequency (1/px)
	double  turbEvolution;				// time offset into the noise field

	// Trail (aux particles) — children shed along each parent's path
	int     trailEnable;				// 0/1
	double  trailParticlesPerSec;		// children spawned per parent per second
	double  trailLife;					// seconds
	double  trailSize;					// px
	double  trailOpacity;				// 0..1
	double  trailInheritVel;			// 0..1 fraction of parent velocity

	// Global
	unsigned int randomSeed;
	double  frameRate;					// simulation steps per second
	double  currentTime;				// seconds to simulate up to

	// Projection
	double  centerX, centerY;			// optical centre (output-pixel space)
	double  focalLength;				// perspective focal length in px
};

// A particle resolved to screen space, ready to be composited.
struct RParticle {
	double  x, y;		// output-pixel centre
	double  radius;		// output-pixel radius
	double  r, g, b;	// 0..1 colour
	double  a;			// 0..1 opacity
	int     type;		// ParticleType
	double  depth;		// camera-space Z (for back-to-front sorting)
	double  angle;		// sprite rotation in radians (Texture / Star)
	double  texSampleTime;	// Texture: source time to sample, in seconds
							// (<0 = sample the current comp frame)
};

// Run the simulation and fill 'out' with the particles alive at currentTime.
void Simulate(const SimParams& params, std::vector<RParticle>& out);

} // namespace pf
