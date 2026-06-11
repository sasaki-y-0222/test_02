#
# CMake build for the ParticleForge After Effects plug-in.
#
# Requirements:
#   * The Adobe After Effects SDK (free; download from
#     https://developer.adobe.com/after-effects/ ). It is NOT redistributed
#     here for licensing reasons.
#   * Windows: Visual Studio 2019/2022 (v142/v143 toolset).
#   * macOS:   Xcode 12+ (universal arm64 + x86_64).
#
# Point CMake at the SDK with -DAESDK_ROOT=/path/to/AfterEffectsSDK
# (the folder that contains "Examples/Headers").
#
#   cmake -B build -DAESDK_ROOT="C:/AE_SDK/AfterEffectsSDK"
#   cmake --build build --config Release
#
# The resulting binary is named ParticleForge.aex (Windows) /
# ParticleForge.plugin (macOS). Copy it into:
#   Windows: ...\Adobe\Common\Plug-ins\7.0\MediaCore\
#   macOS:   /Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/
#

cmake_minimum_required(VERSION 3.16)
project(ParticleForge CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED AESDK_ROOT)
	message(FATAL_ERROR
		"AESDK_ROOT is not set. Re-run with -DAESDK_ROOT=/path/to/AfterEffectsSDK")
endif()

# Common SDK header locations (paths differ slightly between SDK releases;
# all of these are added so any recent SDK layout resolves).
set(AE_HEADER_DIRS
	"${AESDK_ROOT}/Examples/Headers"
	"${AESDK_ROOT}/Examples/Headers/SP"
	"${AESDK_ROOT}/Examples/Util"
	"${AESDK_ROOT}/Examples/Resources"
)

set(PLUGIN_SOURCES
	ParticleForge.cpp
	Simulation.cpp
)

set(PLUGIN_HEADERS
	ParticleForge.h
	Simulation.h
	Noise.h
)

# The SDK ships AEGP_SuiteHandler.cpp / MissingSuiteError.cpp in Examples/Util;
# include whichever exist so AEGP_SuiteHandler links.
foreach(util AEGP_SuiteHandler.cpp MissingSuiteError.cpp)
	if(EXISTS "${AESDK_ROOT}/Examples/Util/${util}")
		list(APPEND PLUGIN_SOURCES "${AESDK_ROOT}/Examples/Util/${util}")
	endif()
endforeach()

if(WIN32)
	add_library(ParticleForge MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})
	target_include_directories(ParticleForge PRIVATE ${AE_HEADER_DIRS})
	set_target_properties(ParticleForge PROPERTIES
		SUFFIX ".aex"
		OUTPUT_NAME "ParticleForge")
	target_compile_definitions(ParticleForge PRIVATE
		MSWindows=1 _CRT_SECURE_NO_WARNINGS)

	# --- Compile and embed the PiPL resource -----------------------------
	# Uses the SDK's PiPLtool to turn the .r into an .rr, then the MS resource
	# compiler embeds it. PiPLtool.exe ships under Examples/Resources.
	find_program(PIPLTOOL PiPLtool
		PATHS "${AESDK_ROOT}/Examples/Resources" "${AESDK_ROOT}/Resources")
	if(PIPLTOOL)
		set(PIPL_RR "${CMAKE_CURRENT_BINARY_DIR}/ParticleForgePiPL.rr")
		set(PIPL_RC "${CMAKE_CURRENT_BINARY_DIR}/ParticleForgePiPL.rc")
		add_custom_command(
			OUTPUT "${PIPL_RC}"
			COMMAND ${CMAKE_CXX_COMPILER} /EP /I "${AESDK_ROOT}/Examples/Headers"
					/DMSWindows=1
					"${CMAKE_CURRENT_SOURCE_DIR}/ParticleForgePiPL.r" > "${PIPL_RR}"
			COMMAND ${PIPLTOOL} "${PIPL_RR}" "${PIPL_RC}"
			DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ParticleForgePiPL.r"
			VERBATIM)
		target_sources(ParticleForge PRIVATE "${PIPL_RC}")
	else()
		message(WARNING
			"PiPLtool not found - build the PiPL resource manually "
			"(see README.md) and add the resulting .rc to the project.")
	endif()

elseif(APPLE)
	add_library(ParticleForge MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})
	target_include_directories(ParticleForge PRIVATE ${AE_HEADER_DIRS})
	set_target_properties(ParticleForge PROPERTIES
		BUNDLE TRUE
		BUNDLE_EXTENSION "plugin"
		OUTPUT_NAME "ParticleForge"
		XCODE_ATTRIBUTE_WRAPPER_EXTENSION "plugin"
		MACOSX_BUNDLE_GUI_IDENTIFIER "com.ys.ParticleForge"
		OSX_ARCHITECTURES "arm64;x86_64")
	target_compile_definitions(ParticleForge PRIVATE macintosh=1)
	# On macOS the PiPL is compiled with Rez into the bundle's resources.
	# See README.md for the Rez invocation.
endif()
