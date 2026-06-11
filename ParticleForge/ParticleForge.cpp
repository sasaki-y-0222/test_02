/*
	ParticleForge.cpp

	Main After Effects entry point and rendering glue for the ParticleForge
	particle generator. The actual particle physics lives in Simulation.cpp;
	this file is responsible for:

		* describing the parameters to After Effects,
		* gathering parameter values at render time,
		* running the simulation, and
		* compositing the resulting particles into the output world
		  (8-bpc and 16-bpc).

	Tested against the After Effects SDK for AE 2021 (17.x) and newer. The
	plug-in is registered through the classic PiPL mechanism (see
	ParticleForgePiPL.r) which is supported by every AE version from 2021 on.
*/

#include "ParticleForge.h"
#include "Simulation.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <type_traits>

// ===========================================================================
// Parameter setup helpers (filling PF_ParamDef directly keeps this robust
// across the minor signature differences between SDK revisions).
// ===========================================================================

static PF_Err AddFloat(
	PF_InData *in_data,
	const char *name,
	double vmin, double vmax,
	double smin, double smax,
	double dflt,
	short precision,
	A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	def.param_type			= PF_Param_FLOAT_SLIDER;
	PF_STRCPY(def.name, name);
	def.uu.id				= id;
	def.u.fs_d.valid_min	= vmin;
	def.u.fs_d.valid_max	= vmax;
	def.u.fs_d.slider_min	= smin;
	def.u.fs_d.slider_max	= smax;
	def.u.fs_d.value		= dflt;
	def.u.fs_d.dephault		= dflt;
	def.u.fs_d.precision	= precision;
	def.u.fs_d.display_flags = 0;
	return PF_ADD_PARAM(in_data, -1, &def);
}

static PF_Err AddAngle(
	PF_InData *in_data,
	const char *name,
	double dfltDegrees,
	A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	def.param_type	= PF_Param_ANGLE;
	PF_STRCPY(def.name, name);
	def.uu.id		= id;
	def.u.ad.value		= (PF_Fixed)(dfltDegrees * 65536.0);
	def.u.ad.dephault	= (PF_Fixed)(dfltDegrees * 65536.0);
	return PF_ADD_PARAM(in_data, -1, &def);
}

static PF_Err AddColor(
	PF_InData *in_data,
	const char *name,
	A_u_char r, A_u_char g, A_u_char b,
	A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	def.param_type	= PF_Param_COLOR;
	PF_STRCPY(def.name, name);
	def.uu.id		= id;
	def.u.cd.value.alpha	= 255;
	def.u.cd.value.red		= r;
	def.u.cd.value.green	= g;
	def.u.cd.value.blue		= b;
	def.u.cd.dephault		= def.u.cd.value;
	return PF_ADD_PARAM(in_data, -1, &def);
}

static PF_Err AddPoint(
	PF_InData *in_data,
	const char *name,
	double xPercent, double yPercent,
	A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	def.param_type	= PF_Param_POINT;
	PF_STRCPY(def.name, name);
	def.uu.id		= id;
	def.u.td.x_dephault		= (A_long)(xPercent * 65536.0);
	def.u.td.y_dephault		= (A_long)(yPercent * 65536.0);
	def.u.td.restrict_bounds = FALSE;
	return PF_ADD_PARAM(in_data, -1, &def);
}

static PF_Err AddPopup(
	PF_InData *in_data,
	const char *name,
	A_short numChoices,
	A_short dflt,
	const char *choices,
	A_long id)
{
	PF_ParamDef def;
	AEFX_CLR_STRUCT(def);
	def.param_type	= PF_Param_POPUP;
	PF_STRCPY(def.name, name);
	def.uu.id		= id;
	def.u.pd.num_choices	= numChoices;
	def.u.pd.dephault		= dflt;
	def.u.pd.value			= dflt;
	def.u.pd.u.namesptr		= choices;
	return PF_ADD_PARAM(in_data, -1, &def);
}

// ===========================================================================
// Command handlers
// ===========================================================================

static PF_Err About(
	PF_InData *in_data, PF_OutData *out_data,
	PF_ParamDef *params[], PF_LayerDef *output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(
		out_data->return_msg,
		"%s v%d.%d\r\rTrapcode Particular (v1.5 era) style 3D particle "
		"generator.\rNon-commercial / educational re-implementation.",
		PF_NAME, MAJOR_VERSION, MINOR_VERSION);
	return PF_Err_NONE;
}

static PF_Err GlobalSetup(
	PF_InData *in_data, PF_OutData *out_data,
	PF_ParamDef *params[], PF_LayerDef *output)
{
	out_data->my_version = PF_VERSION(	MAJOR_VERSION,
										MINOR_VERSION,
										BUG_VERSION,
										STAGE_VERSION,
										BUILD_VERSION);

	// NON_PARAM_VARY: output depends on time, so AE must re-render per frame.
	// DEEP_COLOR_AWARE: we support 16-bpc.
	// PIX_INDEPENDENT: each output pixel is computed independently.
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
						   PF_OutFlag_NON_PARAM_VARY  |
						   PF_OutFlag_PIX_INDEPENDENT;
	out_data->out_flags2 = PF_OutFlag2_NONE;

	return PF_Err_NONE;
}

static PF_Err ParamsSetup(
	PF_InData *in_data, PF_OutData *out_data,
	PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE;

	// ---- Emitter ----------------------------------------------------------
	err |= AddFloat(in_data, "Particles / sec", 0, 100000, 0, 5000, 1000, 0, ID_PARTICLES_SEC);
	err |= AddPopup(in_data, "Emitter Type", 3, kEmitter_Point, EMITTER_TYPE_CHOICES, ID_EMITTER_TYPE);
	err |= AddPoint(in_data, "Position XY", 50, 50, ID_POSITION);
	err |= AddFloat(in_data, "Position Z", -5000, 5000, -1000, 1000, 0, 1, ID_POSITION_Z);
	err |= AddAngle(in_data, "Direction", 0, ID_DIRECTION);
	err |= AddFloat(in_data, "Direction Spread", 0, 100, 0, 100, 20, 1, ID_DIRECTION_SPREAD);
	err |= AddFloat(in_data, "Velocity", 0, 10000, 0, 1000, 150, 1, ID_VELOCITY);
	err |= AddFloat(in_data, "Velocity Random", 0, 100, 0, 100, 30, 1, ID_VELOCITY_RANDOM);
	err |= AddFloat(in_data, "Velocity Distribution", 0, 100, 0, 100, 50, 1, ID_VELOCITY_DISTRIB);
	err |= AddFloat(in_data, "Emitter Size X", 0, 10000, 0, 2000, 50, 1, ID_EMITTER_SIZE_X);
	err |= AddFloat(in_data, "Emitter Size Y", 0, 10000, 0, 2000, 50, 1, ID_EMITTER_SIZE_Y);
	err |= AddFloat(in_data, "Emitter Size Z", 0, 10000, 0, 2000, 50, 1, ID_EMITTER_SIZE_Z);

	// ---- Particle ---------------------------------------------------------
	err |= AddFloat(in_data, "Life [sec]", 0.05, 60, 0.05, 10, 3.0, 2, ID_LIFE);
	err |= AddFloat(in_data, "Life Random", 0, 100, 0, 100, 20, 1, ID_LIFE_RANDOM);
	err |= AddPopup(in_data, "Particle Type", 3, kParticle_GlowSphere, PARTICLE_TYPE_CHOICES, ID_PARTICLE_TYPE);
	err |= AddFloat(in_data, "Size", 0, 500, 0, 100, 12, 1, ID_SIZE);
	err |= AddFloat(in_data, "Size Random", 0, 100, 0, 100, 30, 1, ID_SIZE_RANDOM);
	err |= AddPopup(in_data, "Size over Life", 5, kCurve_Constant, SIZE_CURVE_CHOICES, ID_SIZE_OVER_LIFE);
	err |= AddFloat(in_data, "Opacity", 0, 100, 0, 100, 100, 1, ID_OPACITY);
	err |= AddPopup(in_data, "Opacity over Life", 5, kCurve_FadeOut, OPACITY_CURVE_CHOICES, ID_OPACITY_OVER_LIFE);
	err |= AddColor(in_data, "Birth Color", 255, 220, 120, ID_COLOR_BIRTH);
	err |= AddColor(in_data, "Death Color", 200, 40, 30, ID_COLOR_DEATH);
	err |= AddPopup(in_data, "Blend Mode", 3, kBlend_Add, BLEND_MODE_CHOICES, ID_BLEND_MODE);

	// ---- Physics ----------------------------------------------------------
	err |= AddFloat(in_data, "Gravity", -5000, 5000, -500, 500, 0, 1, ID_GRAVITY);
	err |= AddFloat(in_data, "Air Resistance", 0, 50, 0, 10, 0, 2, ID_AIR_RESISTANCE);
	err |= AddFloat(in_data, "Wind X", -5000, 5000, -500, 500, 0, 1, ID_WIND_X);
	err |= AddFloat(in_data, "Wind Y", -5000, 5000, -500, 500, 0, 1, ID_WIND_Y);
	err |= AddFloat(in_data, "Spin", -720, 720, -360, 360, 0, 1, ID_SPIN);
	err |= AddFloat(in_data, "Turbulence Amount", 0, 10000, 0, 2000, 0, 1, ID_TURB_AMOUNT);
	err |= AddFloat(in_data, "Turbulence Scale", 0.0001, 0.05, 0.0005, 0.02, 0.004, 4, ID_TURB_SCALE);
	err |= AddFloat(in_data, "Turbulence Evolution", -10000, 10000, 0, 100, 0, 2, ID_TURB_EVOLUTION);

	// ---- Global -----------------------------------------------------------
	err |= AddFloat(in_data, "Random Seed", 0, 100000, 0, 1000, 12345, 0, ID_RANDOM_SEED);

	out_data->num_params = PARAM_NUM_PARAMS;
	return err;
}

// ---------------------------------------------------------------------------
// Compositing
// ---------------------------------------------------------------------------

static inline double Clampd(double v, double lo, double hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

template <typename PixT>
static void ClearWorld(PF_LayerDef *world)
{
	for (A_long y = 0; y < world->height; ++y) {
		PixT *row = reinterpret_cast<PixT*>(
			reinterpret_cast<char*>(world->data) + (size_t)y * world->rowbytes);
		for (A_long x = 0; x < world->width; ++x) {
			row[x].alpha = 0;
			row[x].red   = 0;
			row[x].green = 0;
			row[x].blue  = 0;
		}
	}
}

template <typename PixT>
static void CopyInput(PF_LayerDef *in, PF_LayerDef *out)
{
	A_long h = std::min(in->height, out->height);
	A_long w = std::min(in->width,  out->width);
	for (A_long y = 0; y < h; ++y) {
		PixT *src = reinterpret_cast<PixT*>(
			reinterpret_cast<char*>(in->data)  + (size_t)y * in->rowbytes);
		PixT *dst = reinterpret_cast<PixT*>(
			reinterpret_cast<char*>(out->data) + (size_t)y * out->rowbytes);
		for (A_long x = 0; x < w; ++x)
			dst[x] = src[x];
	}
}

// Composite one particle into a world. maxv is the channel maximum
// (255 for 8-bpc, 32768 for 16-bpc).
template <typename PixT>
static void DrawParticle(PF_LayerDef *out, const pf::RParticle &p, int blendMode, double maxv)
{
	double ext = (p.type == pf::kParticle_Sphere) ? (p.radius + 1.5)
												   : (p.radius * 2.5 + 1.5);

	A_long x0 = (A_long)std::floor(p.x - ext);
	A_long x1 = (A_long)std::ceil (p.x + ext);
	A_long y0 = (A_long)std::floor(p.y - ext);
	A_long y1 = (A_long)std::ceil (p.y + ext);

	if (x1 < 0 || y1 < 0 || x0 >= out->width || y0 >= out->height) return;
	x0 = std::max<A_long>(x0, 0);
	y0 = std::max<A_long>(y0, 0);
	x1 = std::min<A_long>(x1, out->width  - 1);
	y1 = std::min<A_long>(y1, out->height - 1);

	const double s = p.radius * 0.6;
	const double twoSigma2 = 2.0 * s * s + 1e-6;

	for (A_long y = y0; y <= y1; ++y) {
		PixT *row = reinterpret_cast<PixT*>(
			reinterpret_cast<char*>(out->data) + (size_t)y * out->rowbytes);
		double dy = (y + 0.5) - p.y;
		for (A_long x = x0; x <= x1; ++x) {
			double dx = (x + 0.5) - p.x;
			double dist2 = dx * dx + dy * dy;
			double cov = 0.0;

			if (p.type == pf::kParticle_Sphere) {
				double dist = std::sqrt(dist2);
				cov = p.radius + 0.5 - dist;	// 1px anti-aliased edge
				if (cov <= 0.0) continue;
				if (cov > 1.0) cov = 1.0;
			} else if (p.type == pf::kParticle_GlowSphere) {
				cov = std::exp(-dist2 / twoSigma2);
				if (cov < 0.004) continue;
			} else { // Star: glow core + axial spikes
				double base = std::exp(-dist2 / twoSigma2);
				double sc   = s * 0.30 + 0.5;
				double spx  = std::exp(-(dy * dy) / (2.0 * sc * sc)) *
							  std::exp(-std::fabs(dx) / (p.radius * 1.2 + 1.0));
				double spy  = std::exp(-(dx * dx) / (2.0 * sc * sc)) *
							  std::exp(-std::fabs(dy) / (p.radius * 1.2 + 1.0));
				cov = std::max(base, std::max(spx, spy));
				if (cov < 0.004) continue;
			}

			double a = cov * p.a;
			if (a <= 0.0) continue;
			if (a > 1.0) a = 1.0;

			PixT &px = row[x];
			double er = px.red   / maxv;
			double eg = px.green / maxv;
			double eb = px.blue  / maxv;
			double ea = px.alpha / maxv;

			switch (blendMode) {
				case pf::kBlend_Normal:
					er = er * (1.0 - a) + p.r * a;
					eg = eg * (1.0 - a) + p.g * a;
					eb = eb * (1.0 - a) + p.b * a;
					break;
				case pf::kBlend_Screen:
					er = 1.0 - (1.0 - er) * (1.0 - p.r * a);
					eg = 1.0 - (1.0 - eg) * (1.0 - p.g * a);
					eb = 1.0 - (1.0 - eb) * (1.0 - p.b * a);
					break;
				case pf::kBlend_Add:
				default:
					er += p.r * a;
					eg += p.g * a;
					eb += p.b * a;
					break;
			}
			ea = ea + a * (1.0 - ea);

			px.red   = (typename std::remove_reference<decltype(px.red)>::type)(Clampd(er, 0.0, 1.0) * maxv + 0.5);
			px.green = (typename std::remove_reference<decltype(px.green)>::type)(Clampd(eg, 0.0, 1.0) * maxv + 0.5);
			px.blue  = (typename std::remove_reference<decltype(px.blue)>::type)(Clampd(eb, 0.0, 1.0) * maxv + 0.5);
			px.alpha = (typename std::remove_reference<decltype(px.alpha)>::type)(Clampd(ea, 0.0, 1.0) * maxv + 0.5);
		}
	}
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

static double FixedToDouble(PF_Fixed f) { return (double)f / 65536.0; }

static PF_Err Render(
	PF_InData *in_data, PF_OutData *out_data,
	PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE;

	const double dsx = (double)in_data->downsample_x.num / in_data->downsample_x.den;
	const double dsy = (double)in_data->downsample_y.num / in_data->downsample_y.den;
	const double ds  = (dsx + dsy) * 0.5;	// uniform length scale

	// --- gather parameters into the simulation struct ----------------------
	pf::SimParams sp;

	sp.particlesPerSec	= params[PARAM_PARTICLES_SEC]->u.fs_d.value;
	sp.emitterType		= params[PARAM_EMITTER_TYPE]->u.pd.value;

	sp.posX = FixedToDouble(params[PARAM_POSITION]->u.td.x_value) * dsx;
	sp.posY = FixedToDouble(params[PARAM_POSITION]->u.td.y_value) * dsy;
	sp.posZ = params[PARAM_POSITION_Z]->u.fs_d.value * ds;

	sp.direction		= FixedToDouble(params[PARAM_DIRECTION]->u.ad.value) * 3.14159265358979 / 180.0;
	sp.directionSpread	= params[PARAM_DIRECTION_SPREAD]->u.fs_d.value / 100.0;
	sp.velocity			= params[PARAM_VELOCITY]->u.fs_d.value * ds;
	sp.velocityRandom	= params[PARAM_VELOCITY_RANDOM]->u.fs_d.value / 100.0;
	sp.velocityDistribution = params[PARAM_VELOCITY_DISTRIB]->u.fs_d.value / 100.0;
	sp.emitterSizeX		= params[PARAM_EMITTER_SIZE_X]->u.fs_d.value * dsx;
	sp.emitterSizeY		= params[PARAM_EMITTER_SIZE_Y]->u.fs_d.value * dsy;
	sp.emitterSizeZ		= params[PARAM_EMITTER_SIZE_Z]->u.fs_d.value * ds;

	sp.life				= params[PARAM_LIFE]->u.fs_d.value;
	sp.lifeRandom		= params[PARAM_LIFE_RANDOM]->u.fs_d.value / 100.0;
	sp.particleType		= params[PARAM_PARTICLE_TYPE]->u.pd.value;
	sp.size				= params[PARAM_SIZE]->u.fs_d.value * ds;
	sp.sizeRandom		= params[PARAM_SIZE_RANDOM]->u.fs_d.value / 100.0;
	sp.sizeOverLife		= params[PARAM_SIZE_OVER_LIFE]->u.pd.value;
	sp.opacity			= params[PARAM_OPACITY]->u.fs_d.value / 100.0;
	sp.opacityOverLife	= params[PARAM_OPACITY_OVER_LIFE]->u.pd.value;

	sp.colorBirth[0] = params[PARAM_COLOR_BIRTH]->u.cd.value.red   / 255.0;
	sp.colorBirth[1] = params[PARAM_COLOR_BIRTH]->u.cd.value.green / 255.0;
	sp.colorBirth[2] = params[PARAM_COLOR_BIRTH]->u.cd.value.blue  / 255.0;
	sp.colorDeath[0] = params[PARAM_COLOR_DEATH]->u.cd.value.red   / 255.0;
	sp.colorDeath[1] = params[PARAM_COLOR_DEATH]->u.cd.value.green / 255.0;
	sp.colorDeath[2] = params[PARAM_COLOR_DEATH]->u.cd.value.blue  / 255.0;
	sp.blendMode		= params[PARAM_BLEND_MODE]->u.pd.value;

	sp.gravity			= params[PARAM_GRAVITY]->u.fs_d.value * ds;
	sp.airResistance	= params[PARAM_AIR_RESISTANCE]->u.fs_d.value;
	sp.windX			= params[PARAM_WIND_X]->u.fs_d.value * ds;
	sp.windY			= params[PARAM_WIND_Y]->u.fs_d.value * ds;
	sp.spin				= params[PARAM_SPIN]->u.fs_d.value;
	sp.turbAmount		= params[PARAM_TURB_AMOUNT]->u.fs_d.value * ds;
	sp.turbScale		= params[PARAM_TURB_SCALE]->u.fs_d.value / ds;	// keep look across resolutions
	sp.turbEvolution	= params[PARAM_TURB_EVOLUTION]->u.fs_d.value;

	sp.randomSeed		= (unsigned int)(params[PARAM_RANDOM_SEED]->u.fs_d.value + 0.5);

	// time and frame rate
	double timeScale = (double)in_data->time_scale;
	sp.currentTime = (timeScale > 0.0) ? (double)in_data->current_time / timeScale : 0.0;
	double frameDur = (in_data->time_step > 0 && timeScale > 0.0)
						? (double)in_data->time_step / timeScale
						: (1.0 / 30.0);
	sp.frameRate = (frameDur > 0.0) ? (1.0 / frameDur) : 30.0;

	// projection
	sp.centerX = output->width  * 0.5;
	sp.centerY = output->height * 0.5;
	sp.focalLength = std::max<double>(output->width, output->height);

	// --- run the simulation ------------------------------------------------
	std::vector<pf::RParticle> particles;
	pf::Simulate(sp, particles);

	// --- build the output --------------------------------------------------
	PF_LayerDef *input = &params[PARAM_INPUT]->u.ld;
	bool deep = PF_WORLD_IS_DEEP(output);

	if (deep) {
		ClearWorld<PF_Pixel16>(output);
		CopyInput<PF_Pixel16>(input, output);
		const double maxv = (double)PF_MAX_CHAN16;
		for (const pf::RParticle &p : particles)
			DrawParticle<PF_Pixel16>(output, p, sp.blendMode, maxv);
	} else {
		ClearWorld<PF_Pixel8>(output);
		CopyInput<PF_Pixel8>(input, output);
		const double maxv = (double)PF_MAX_CHAN8;
		for (const pf::RParticle &p : particles)
			DrawParticle<PF_Pixel8>(output, p, sp.blendMode, maxv);
	}

	return err;
}

// ===========================================================================
// Entry point
// ===========================================================================

DllExport PF_Err EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra)
{
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:
				err = About(in_data, out_data, params, output);
				break;
			case PF_Cmd_GLOBAL_SETUP:
				err = GlobalSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_PARAMS_SETUP:
				err = ParamsSetup(in_data, out_data, params, output);
				break;
			case PF_Cmd_RENDER:
				err = Render(in_data, out_data, params, output);
				break;
			default:
				break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	} catch (...) {
		err = PF_Err_INTERNAL_STRUCT_DAMAGED;
	}
	return err;
}
