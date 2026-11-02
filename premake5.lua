--==============================================================================
-- premake5.lua — REFERENCE GENERATOR for build/skygfx.vcxproj
--
-- *** build/skygfx.vcxproj IS THE AUTHORITATIVE BUILD INPUT. ***
-- It is hand-maintained and tools/fast_build.py builds it with MSBuild
-- directly; premake is NOT part of the normal build. This script exists only so
-- that IF someone runs premake, the regenerated project behaves identically to
-- the live one. It is a mirror of the vcxproj, not the source of truth.
--
-- Verified against: premake5 5.0.0-alpha10  (./premake5.exe, action vs2015).
-- premake5.cmd invokes exactly that action. Do not run premake on this tree
-- without the verification procedure at the bottom of this header.
--
-- HAND-MAINTAINED OVERRIDES THIS SCRIPT MUST REPRODUCE
-- ----------------------------------------------------
-- 1. src/texdefs.cpp is a top-level TU merging the former src/extras/texdb.cpp,
--    src/render/normalmap.cpp and src/rw/normalmap.cpp. The duplicate
--    normalmap.cpp basenames are gone, so NO per-file ObjectFileName /
--    objectname override exists in the project or in this script.
-- 2. Dead code is DELETED FROM DISK (dead-code sweep 2026-10-25). The 15
--    removed files are: vegImposter.{cpp,h}, HLSL_hook.{cpp,h}, SMAA.{cpp,h},
--    skygfx_bridge.cpp, normalmap_plugin.cpp, defaultFuncs.cpp,
--    utilitynoise.cpp, test.cpp, EdgeDetection.h,
--    PostFXPipeline.h, GrassSystem.h, hooks.h, PC_PlantsMgr.cpp,
--    PC_PlantsMgr_overlay.cpp (plus config.cpp, which the live vcxproj had
--    already commented out). Because they no longer exist, the directory globs
--    below cannot pick them up and NO removefiles() guard is required. If any
--    of them is ever restored, re-add an explicit removefiles() entry in the
--    same commit so a glob cannot resurrect it.
-- 3. src/render/PC_PlantsMgr.h SURVIVED the 2026-10-25 sweep but is now MERGED
--    (2026-10-27) into src/render/grasssys.h together with PC_GrassRenderer.h,
--    PC_GrassInstancer.h and the two grass TUs (now one grasssys.cpp TU).
--    The 3-line forwarding headers (neo.h, PC_GrassInstancer.h) were deleted
--    2026-11-02 (M6); includers use neopipe.h / grasssys.h directly.
--    PC_PlantsMgr.h and PC_GrassRenderer.h are deleted.
-- 4. New TUs that must be compiled (covered by globs and repeated explicitly
--    as a tripwire): src/render/texalpha.cpp, src/render/noise.cpp,
--    src/render/renderstate_monitor.cpp, the split src/render/postfx_*.cpp,
--    src/core/debugmenu_theme.cpp and src/core/debugmenu_ui.cpp.
-- 5. Case sensitivity: this project uses src/core (lower-case). The previous
--    script globbed src/Core, which only worked because Windows paths are
--    case-insensitive; on a case-sensitive filesystem debugmenu_ui.cpp and
--    debugmenu_theme.cpp were silently dropped. Every path below is exact.
-- 6. Shaders are NOT built by the project. tools/fast_build.py --shaders
--    compiles all 110 multi-entry outputs into resources/cso/*.cso
--    (VehiclePBR_Modern.hlsl alone has 7 entry points). The live
--    PreBuildEvent is literally "rem Shaders precompiled manually", so that is
--    what is reproduced here. Do NOT reintroduce fxc loops: they rebuild 110
--    CSOs on every incremental build and desync them from fast_build.py.
-- 7. resources/Resource.rc and resources/VersionInfo.rc are the two
--    ResourceCompile inputs. Resource.rc RCDATA-embeds resources/cso/*.cso and
--    includes resources/resource.h; VersionInfo.rc includes VersionInfo.h.
--
-- NOT EXPRESSIBLE IN premake 5.0.0-alpha10 — REVIEW BY HAND AFTER REGENERATING
-- ----------------------------------------------------------------------------
--   * <UseEnv>true</UseEnv> in Globals          — no API; MSBuild default is false.
--   * <WindowsSdkDir> PropertyGroup at EOF      — legacy hand-added property.
--   * <ExternalWarningLevel>Level3</...>        — no API; MSBuild default Level3,
--                                                 so behaviourally equivalent.
--   * <ImageHasSafeExceptionHandlers>false</...>— no API; MSBuild default false,
--                                                 so behaviourally equivalent.
--   * <MapFile>$(SkyGfxGameDir)skygfx.map</MapFile>— only GenerateMapFile is
--                                                 emitted; the default
--                                                 $(OutDir)$(TargetName).map
--                                                 resolves to the same file.
--   * <SkyGfxGameDir>E:\games\gtasa_skygfx_plus\</SkyGfxGameDir>
--                                               — no API for a custom MSBuild
--                                                 property or a /p: override;
--                                                 targetdir below carries the
--                                                 literal path instead.
--   * ResourceCompile's /I list will also contain third_party/bullet/src and the
--     Windows Kit um directory, and its defines will also carry
--     _CRT_NONSTDC_NO_WARNINGS/_CRT_SECURE_NO_WARNINGS. Both are ignored by
--     rc.exe but show up in the XML diff; harmless.
--
-- REGENERATION + VERIFICATION (scratch worktree; never in place)
-- -------------------------------------------------------------
--   git -C "E:/dev/dave/skygfx_plus_expIV" worktree add --detach \
--       ../skygfx_premake_verify HEAD
--   cd ../skygfx_premake_verify
--
--   # HEAD is NOT a usable snapshot of the working tree:
--   #   * the 15 dead files are deleted only in the working tree, so HEAD still
--   #     has them and the globs would re-add them;
--   #   * the new TUs (texalpha, noise, profiler, mirrorPass, postfx_*, ...) are
--   #     untracked, and third_party/bullet is gitignored, so HEAD lacks them.
--   # Replace the input trees with the live working tree before generating:
--   rm -rf src resources external shaders third_party/bullet
--   cp -r "E:/dev/dave/skygfx_plus_expIV/src" src
--   cp -r "E:/dev/dave/skygfx_plus_expIV/resources" resources
--   cp -r "E:/dev/dave/skygfx_plus_expIV/external" external
--   cp -r "E:/dev/dave/skygfx_plus_expIV/shaders" shaders
--   cp -r "E:/dev/dave/skygfx_plus_expIV/third_party/bullet" third_party/bullet
--   cp "E:/dev/dave/skygfx_plus_expIV/premake5.lua" .
--
--   "E:/dev/dave/skygfx_plus_expIV/premake5.exe" --file=premake5.lua vs2015
--   diff -u "E:/dev/dave/skygfx_plus_expIV/build/skygfx.vcxproj" \
--           build/skygfx.vcxproj
--   cd "E:/dev/dave/skygfx_plus_expIV"
--   git worktree remove --force ../skygfx_premake_verify
--
--   Run from inside the worktree (as above) so location "build" and all
--   relative source globs resolve against the scratch copy. Never run premake
--   against the main working tree: it would overwrite the authoritative
--   build/skygfx.vcxproj and its filters.
--
--   premake 5.0.0-alpha10 supports --file= but NOT --to= (checked with
--   ./premake5.exe --help), hence the worktree. Expected cosmetic diffs:
--   entry order (premake sorts by vpath), per-config Condition attributes,
--   explicit default-valued elements (e.g. DebugInformationFormat None), and a
--   rewritten .vcxproj.filters (the live filters file still holds pre-merge
--   paths and is not used for compilation).
--
--   Functional gates — all must hold in the regenerated vcxproj:
--     * exactly 169 ClCompile: 48 project TUs + 115 bullet + 6 imgui
--       (57 project TUs after the texdefs merge, minus 11 retired/merged
--        2026-10-27: 3 neo TUs -> neopipe.cpp, 2 grass TUs -> grasssys.cpp,
--        6 retired mode stubs deleted; config.cpp stays absent)
--     * no ClCompile / ClInclude for any of the 15 deleted files
--       (vegImposter, HLSL_hook, SMAA, skygfx_bridge, normalmap_plugin,
--        test, EdgeDetection.h, PostFXPipeline.h, GrassSystem.h, hooks.h,
--        PC_PlantsMgr.cpp, PC_PlantsMgr_overlay.cpp)
--     * src/render/grasssys.h + src/render/neopipe.h are present as ClInclude
--       (PC_PlantsMgr.h / PC_GrassRenderer.h merged away 2026-10-27)
--     * BOTH normalmap.cpp TUs are GONE; src/texdefs.cpp is present as
--       ClCompile and src/texdefs.h as ClInclude, with no ObjectFileName
--       overrides anywhere
--     * texalpha / noise / renderstate_monitor / profiler / treeWind /
--       mirrorPass / forwardplus / postfx + all postfx_* / debugmenu_theme /
--       debugmenu_ui present
--     * ResourceCompile for Resource.rc and VersionInfo.rc
--     * PreBuildEvent  = rem Shaders precompiled manually
--     * PostBuildEvent = copy ... skygfx.asi && del ... skygfx.dll
--     * PlatformToolset v143, CharacterSet MultiByte, OutDir game dir,
--       TargetName skygfx, AdditionalDependencies include rpnormmap.lib
--
-- Last fully executed verification: 2026-10-25 (see
-- docs/plans/2026-10-25-premake-verification.md). The live vcxproj is
-- hand-edited concurrently, so always diff against its CURRENT contents.
--==============================================================================

--==============================================================================
-- Workspace / project shape
--==============================================================================
workspace "skygfx"
	configurations { "Release", "Debug" }
	location "build"

project "skygfx"
	-- Match the live project GUID so build/skygfx.sln does not churn either.
	uuid "A1A0F41B-8D42-1FF2-76FD-3246623EA642"
	kind "SharedLib"
	language "C++"
	namespace "skygfx"
	characterset "MBCS"
	toolset "v143"
	-- Live vcxproj pins WindowsTargetPlatformVersion=10.0.19041.0.
	systemversion "10.0.19041.0"

	targetname "skygfx"
	targetextension ".dll"
	-- The live project routes OutDir/ImportLibrary/MapFile and the ASI copy
	-- through <SkyGfxGameDir>, overridable with /p:SkyGfxGameDir=<dir>.
	-- premake cannot emit that property (nor consume the override), so the
	-- concrete default path is spelled here; edit it to match if you move the
	-- game dir. See "NOT EXPRESSIBLE" in the header.
	targetdir "E:/games/gtasa_skygfx_plus"
	-- Live IntDir is $(ProjectDir)obj\<Config>\. A plain "obj" also resolves
	-- against the project dir, but spelling it exactly removes an XML delta.
	objdir "$(ProjectDir)obj"

	--==========================================================================
	-- Source files
	--
	-- src/ is deliberately NOT globbed as a whole: a single stale glob is what
	-- destroyed the hand overrides. Directories are case-correct and exact.
	--==========================================================================

	-- src/core: glob is complete today (crashhandler, debugmenu_theme,
	-- debugmenu_ui, diagnostics, main, menu_inject, modeManager, presets,
	-- weather). config.cpp and test.cpp were deleted in the 2026-10-25 sweep.
	files { "src/core/*.cpp" }

	-- src/entities: chars.cpp, sftbdyphsx.cpp (merged ragdoll + dismemberment)
	files { "src/entities/*.cpp" }

	-- src/render: plain glob. The dead files (SMAA.cpp, vegImposter.cpp,
	-- PC_PlantsMgr.cpp, PC_PlantsMgr_overlay.cpp, utilitynoise.cpp) no longer
	-- exist on disk (utilitynoise deleted 2026-10-27; superseded by SkyNoise).
	files { "src/render/*.cpp" }

	-- src/rw: plain glob (normalmap_plugin.cpp, HLSL_hook.cpp and defaultFuncs.cpp
	-- are deleted; defaultFuncs removed 2026-10-27 - zero cross-TU references).
	files { "src/rw/*.cpp" }

	-- src/extras: plain glob (skygfx_bridge.cpp is deleted).
	files { "src/extras/*.cpp" }

	-- src/peds: peds_pipe module (Phase 1 skeleton — peds_pipe + skin;
	-- hair/cloth sub-modules land in later phases per
	-- docs/plans/2026-10-31-peds-pipe-design.md).
	files { "src/peds/*.cpp" }

	-- Top-level TUs (src/texdefs.cpp: unified texture-definition layer).
	files { "src/*.cpp" }

	-- No removefiles() guard: every dead TU was deleted from disk in the
	-- 2026-10-25 sweep, so no glob can see it. If one is ever restored,
	-- re-add an explicit removefiles() entry here in the same commit.

	-- New / critical TUs re-asserted explicitly (redundant with the globs, but
	-- they survive a future glob narrowing or a directory rename).
	files {
		"src/texdefs.cpp",
		"src/render/texalpha.cpp",
		"src/render/noise.cpp",
		"src/render/renderstate_monitor.cpp",
		"src/render/profiler.cpp",
		"src/render/neopipe.cpp",
		"src/render/grasssys.cpp",
		"src/render/particles_gpu.cpp",	-- m0336 GPU particle lane (glob-covered, tripwire)
		"src/render/treeWind.cpp",
		"src/render/mirrorPass.cpp",
		"src/render/forwardplus.cpp",
		"src/render/postfx.cpp",
		"src/render/postfx_colourfilter.cpp",
		"src/render/postfx_luminance.cpp",
		"src/render/postfx_motionblur.cpp",
		"src/render/postfx_newfx.cpp",
		"src/render/postfx_smaa.cpp",
		"src/render/postfx_ssao.cpp",
		"src/render/postfx_utils.cpp",
		"src/render/postfx_underwater.cpp",
		"src/core/debugmenu_theme.cpp",
		"src/core/debugmenu_ui.cpp",
	}

	-- Headers: exact list from the live vcxproj (IDE parity only; ClInclude has
	-- no effect on the build). Not globbed, so regeneration does not add noise.
	files {
		"external/ini_parser.hpp",
		"resources/VersionInfo.h",
		"resources/resource.h",
		"src/skygfx.h",
		"src/texdefs.h",
		"src/rw/LinkList.h",
		"src/rw/MemoryMgr.h",
		"src/rw/ModuleList.hpp",
		"src/rw/Pools.h",
		"src/rw/gta.h",
		"src/render/neopipe.h",
		"src/render/particles_gpu.h",	-- m0336 GPU particle lane
		"src/render/grasssys.h",
		"src/render/postfx.h",
		"src/render/noise.h",
		"src/render/profiler.h",
		"src/render/renderstate_monitor.h",
		"src/entities/sftbdyphsx.h",
		"src/core/debug_handl.h",
		"src/core/modeManager.h",
		"src/core/debugmenu_theme.h",
	}

	-- Resource inputs (RCDATA-embeds resources/cso/*.cso via Resource.rc).
	files {
		"resources/Resource.rc",
		"resources/VersionInfo.rc",
	}

	-- Non-compiled project items that the live vcxproj lists under <None>.
	files {
		"resources/Resource.aps",
		"shaders/shaders.bat",
	}

	-- ImGui (absolute SDK paths, same as the live project).
	files {
		"E:/SDKs/imgui-master/imgui.cpp",
		"E:/SDKs/imgui-master/imgui_draw.cpp",
		"E:/SDKs/imgui-master/imgui_tables.cpp",
		"E:/SDKs/imgui-master/imgui_widgets.cpp",
		"E:/SDKs/imgui-master/backends/imgui_impl_dx9.cpp",
		"E:/SDKs/imgui-master/backends/imgui_impl_win32.cpp",
	}

	-- Bullet Physics (vendored v2.87). Recursive glob: 115 .cpp files, exactly
	-- the set in the live vcxproj (verified against build/skygfx.vcxproj).
	files { "third_party/bullet/src/**.cpp" }

	--==========================================================================
	-- File-level overrides
	--==========================================================================

	-- (PC_PlantsMgr*.cpp used to be listed with ExcludedFromBuild; both files
	-- were deleted in the 2026-10-25 sweep, so there is nothing to exclude.)
	filter {}

	--==========================================================================
	-- Compiler / linker settings (mirror of the live ItemDefinitionGroups)
	--==========================================================================
	defines {
		'rsc_CompanyName="aap"',
		'rsc_LegalCopyright=""',
		'rsc_FileVersion="4.2.0.0"',
		'rsc_ProductVersion="4.2.0.0"',
		'rsc_InternalName="skygfx"',
		'rsc_ProductName="skygfx"',
		'rsc_OriginalFilename="skygfx.dll"',
		'rsc_FileDescription="https://github.com/aap"',
		'rsc_UpdateUrl="https://github.com/aap/skygfx"',
		"_CRT_NONSTDC_NO_WARNINGS",
		"_CRT_SECURE_NO_WARNINGS",
	}

	buildoptions { "/FS", "/Zc:threadSafeInit-" }
	flags { "StaticRuntime" }
	symbols "Off"                       -- Release default; Debug overrides below

	includedirs {
		"third_party/bullet/src",
		"E:/SDKs/imgui-master",
		"E:/SDKs/imgui-master/backends",
		"E:/SDKs/plugin-sdk-master/plugin_sa",
		"E:/SDKs/normalmap_byDK_1.01/normalmap_byDK_1.01/sources",
		"E:/dev(dave)/Sa_dev/GTASource/GTA SA RE/source",
		"external/injector/include",
		"external",
		"external/d3d9",
		"resources",
		"shaders",
		"src",
		"src/rw",
		"src/render",
		"src/core",
		"src/entities",
		"src/extras",
		"E:/SDKs/Renderware 3.7 SDK (For Windows) Full/RWSDK37/Graphics/rwsdk/include/d3d9",
		"C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include",
		"$(WindowsSDK_IncludePath)",
	}

	libdirs {
		"C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x86",
		"E:/SDKs/normalmap_byDK_1.01/normalmap_byDK_1.01/sources",
		"E:/SDKs/Renderware 3.7 SDK (For Windows) Full/RWSDK37/Graphics/rwsdk/lib/d3d9/release",
	}

	-- Same order as the live AdditionalDependencies. The trailing kernel32...
	-- set is the legacy default-library list kept verbatim for diff parity.
	links {
		"d3d9",
		"d3dx9",
		"winmm",
		"rpnormmap",
		"dbghelp",
		"kernel32",
		"user32",
		"gdi32",
		"winspool",
		"comdlg32",
		"advapi32",
		"shell32",
		"ole32",
		"oleaut32",
		"uuid",
		"odbc32",
		"odbccp32",
	}

	-- Shaders are compiled by tools/fast_build.py --shaders, not by MSBuild.
	-- This is the exact live PreBuildEvent.
	prebuildcommands { "rem Shaders precompiled manually" }
	-- If shaders ever need to be built from MSBuild again, do NOT hand-roll an
	-- fxc loop here: VehiclePBR_Modern.hlsl alone has 7 entry points and the
	-- full set is 110 outputs (see the multi_entry table in
	-- tools/fast_build.py). A naive per-file loop writes the wrong .cso names
	-- and desyncs resources/cso from the resource script.

	-- Copy to the ASI name and delete the .dll, exactly like the live project.
	postbuildcommands {
		'copy /y "$(TargetPath)" "$(OutDir)skygfx.asi" && del /f "$(TargetPath)"',
	}

	--==========================================================================
	-- Per-configuration settings
	--==========================================================================
	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "Full"                 -- /Ox + FunctionLevelLinking + Intrinsics
		                                -- + StringPooling + MinimalRebuild=false
		-- Emits <GenerateMapFile>true</GenerateMapFile>. The default
		-- $(TargetDir)$(TargetName).map resolves to the same
		-- E:\games\gtasa_skygfx_plus\skygfx.map as the hand-written <MapFile>.
		flags { "Maps" }

	filter "configurations:Debug"
		defines { "DEBUG" }
		optimize "Off"
		symbols "On"                    -- /Zi + EditAndContinue on x86
	filter {}

	-- NOTE: debugdir/debugcommand are intentionally NOT set. build/skygfx.vcxproj.user
	-- is hand-maintained (local debugger paths) and premake would overwrite it.
