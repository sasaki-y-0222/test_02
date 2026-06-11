/*
	ParticleForge.h

	A Trapcode Particular (v1.5 era) style 3D particle generator for
	Adobe After Effects 2021 (17.x) and later.

	This header declares the plug-in entry point, the parameter layout and
	version information. The heavy lifting (the particle simulation) lives in
	the SDK-independent files Simulation.{h,cpp}.
*/

#pragma once

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AE_GeneralPlug.h"
// #include "AEFX_ChannelDepthTbl.h"
#include "AEGP_SuiteHandler.h"

#ifdef AE_OS_WIN
	#define NOMINMAX
	#include <Windows.h>
#endif

// ---------------------------------------------------------------------------
// Version

#define PF_NAME				"ParticleForge"
#define PF_MATCH_NAME		"YS ParticleForge"
#define PF_CATEGORY			"Simulation"

#define	MAJOR_VERSION		1
#define	MINOR_VERSION		5
#define	BUG_VERSION			0
#define	STAGE_VERSION		PF_Stage_RELEASE
#define	BUILD_VERSION		1

// ---------------------------------------------------------------------------
// Parameter indices. Order here MUST match the order parameters are added in
// ParamsSetup() and matches the layout of the params[] array at render time.

// UI order. GROUP_START/END topic markers are real params and occupy slots in
// the params[] array, so they get their own PARAM_* entries here. Render() only
// reads the value-bearing entries; it never touches the group markers.
enum {
	PARAM_INPUT = 0,

	// --- Emitter -----------------------------------------------------------
	PARAM_EMITTER_GROUP,		// GROUP_START
	PARAM_PARTICLES_SEC,
	PARAM_EMITTER_TYPE,			// SUPERVISE: grays out Emitter Size when Point
	PARAM_EMITTER_SIZE_X,		// moved up: right after Emitter Type
	PARAM_EMITTER_SIZE_Y,
	PARAM_EMITTER_SIZE_Z,
	PARAM_POSITION,				// PF_Param_POINT  (XY)
	PARAM_POSITION_Z,
	PARAM_DIRECTION,			// PF_Param_ANGLE
	PARAM_DIRECTION_SPREAD,
	PARAM_VELOCITY,
	PARAM_VELOCITY_RANDOM,
	PARAM_VELOCITY_DISTRIB,
	PARAM_EMITTER_GROUP_END,	// GROUP_END

	// --- Particle ----------------------------------------------------------
	PARAM_PARTICLE_GROUP,		// GROUP_START
	PARAM_LIFE,
	PARAM_LIFE_RANDOM,
	PARAM_PARTICLE_TYPE,
	PARAM_SIZE,
	PARAM_SIZE_RANDOM,
	PARAM_SIZE_OVER_LIFE,
	PARAM_OPACITY,
	PARAM_OPACITY_OVER_LIFE,
	PARAM_COLOR_BIRTH,
	PARAM_COLOR_DEATH,
	PARAM_BLEND_MODE,
	PARAM_PARTICLE_GROUP_END,	// GROUP_END

	// --- Physics -----------------------------------------------------------
	PARAM_PHYSICS_GROUP,		// GROUP_START
	PARAM_GRAVITY,
	PARAM_AIR_RESISTANCE,
	PARAM_WIND_X,
	PARAM_WIND_Y,
	PARAM_SPIN,
	PARAM_TURB_AMOUNT,
	PARAM_TURB_SCALE,
	PARAM_TURB_EVOLUTION,
	PARAM_PHYSICS_GROUP_END,	// GROUP_END

	// --- Global ------------------------------------------------------------
	PARAM_GLOBAL_GROUP,			// GROUP_START
	PARAM_RANDOM_SEED,
	PARAM_GLOBAL_GROUP_END,		// GROUP_END

	PARAM_NUM_PARAMS
};

// Persistent ("disk") IDs. Never reorder/reuse these once shipped or saved
// projects will read the wrong values. They are independent of UI order.
// IDs 1..32 are the original 1.5 shipping set; anything new is APPENDED.
enum {
	ID_PARTICLES_SEC = 1,
	ID_EMITTER_TYPE,
	ID_POSITION,
	ID_POSITION_Z,
	ID_DIRECTION,
	ID_DIRECTION_SPREAD,
	ID_VELOCITY,
	ID_VELOCITY_RANDOM,
	ID_VELOCITY_DISTRIB,
	ID_EMITTER_SIZE_X,
	ID_EMITTER_SIZE_Y,
	ID_EMITTER_SIZE_Z,
	ID_LIFE,
	ID_LIFE_RANDOM,
	ID_PARTICLE_TYPE,
	ID_SIZE,
	ID_SIZE_RANDOM,
	ID_SIZE_OVER_LIFE,
	ID_OPACITY,
	ID_OPACITY_OVER_LIFE,
	ID_COLOR_BIRTH,
	ID_COLOR_DEATH,
	ID_BLEND_MODE,
	ID_GRAVITY,
	ID_AIR_RESISTANCE,
	ID_WIND_X,
	ID_WIND_Y,
	ID_SPIN,
	ID_TURB_AMOUNT,
	ID_TURB_SCALE,
	ID_TURB_EVOLUTION,
	ID_RANDOM_SEED,				// = 32 (last of the original set)

	// --- appended after 1.5 shipping; never reorder/reuse -----------------
	ID_EMITTER_GROUP = 33,
	ID_EMITTER_GROUP_END,
	ID_PARTICLE_GROUP,
	ID_PARTICLE_GROUP_END,
	ID_PHYSICS_GROUP,
	ID_PHYSICS_GROUP_END,
	ID_GLOBAL_GROUP,
	ID_GLOBAL_GROUP_END
};

// Popup string lists (1-based)
#define EMITTER_TYPE_CHOICES	"Point|Box|Sphere"
#define PARTICLE_TYPE_CHOICES	"Sphere|Glow Sphere|Star"
#define SIZE_CURVE_CHOICES		"Constant|Grow|Shrink|Grow then Shrink|Bell"
#define OPACITY_CURVE_CHOICES	"Constant|Fade In|Fade Out|Fade In and Out|Bell"
#define BLEND_MODE_CHOICES		"Add|Normal|Screen"

#ifdef __cplusplus
extern "C" {
#endif

DllExport PF_Err EffectMain(
	PF_Cmd			cmd,
	PF_InData		*in_data,
	PF_OutData		*out_data,
	PF_ParamDef		*params[],
	PF_LayerDef		*output,
	void			*extra);

#ifdef __cplusplus
}
#endif
