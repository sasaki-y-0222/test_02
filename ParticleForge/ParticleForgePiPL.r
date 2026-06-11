/*
	ParticleForgePiPL.r

	PiPL resource describing the plug-in to After Effects. Compiled on Windows
	with the SDK's PiPLtool (invoked automatically by the supplied CMake/VS
	build) and on macOS with Rez.

	The Global_OutFlags / OutFlags_2 values below MUST match those set in
	GlobalSetup() in ParticleForge.cpp:
		out_flags  = PF_OutFlag_DEEP_COLOR_AWARE  (1<<25 = 0x02000000)
				   | PF_OutFlag_NON_PARAM_VARY    (1<<2  = 0x00000004)
				   | PF_OutFlag_PIX_INDEPENDENT   (1<<10 = 0x00000400)
				   => 0x02000404
		out_flags2 = 0
*/

#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"ParticleForge"
		},
		/* [3] */
		Category {
			"Simulation"
		},

#ifdef AE_OS_WIN
		/* [4] */
		CodeWin64X86 {"EffectMain"},
#else
		/* [4] */
		CodeMacARM64 {"EffectMain"},
		/* [5] */
		CodeMacIntel64 {"EffectMain"},
#endif

		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			/* 1.5.0 release, build 1 :
			   (1<<19)|(5<<15)|(0<<11)|(PF_Stage_RELEASE<<9)|(1)
			   = 524288 + 163840 + 0 + 1536 + 1 = 689665 */
			689665
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
			0x02000404
		},
		AE_Effect_Global_OutFlags_2 {
			0x00000000
		},
		/* [11] */
		AE_Effect_Match_Name {
			"YS ParticleForge"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		}
	}
};
