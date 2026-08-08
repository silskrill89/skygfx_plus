workspace "skygfx"
	configurations { "Release", "Debug" }
	location "build"
   
	defines { "rsc_CompanyName=\"aap\"" }
	defines { "rsc_LegalCopyright=\"\""} 
	defines { "rsc_FileVersion=\"4.2.0.0\"", "rsc_ProductVersion=\"4.2.0.0\"" }
	defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name}\"", "rsc_OriginalFilename=\"%{prj.name}.dll\"" }
	defines { "rsc_FileDescription=\"https://github.com/aap\"" }
	defines { "rsc_UpdateUrl=\"https://github.com/aap/skygfx\"" }
   
	files { "external/*.*" }
	files { "resources/*.*" }
	files { "shaders/*.*" }
	files { "src/*.*" }
	files { "src/Core/*.*" }
	files { "src/entities/*.*" }
	files { "src/extras/*.*" }
	files { "src/render/*.*" }
	files { "src/rw/*.*" }
	files { "E:/SDKs/imgui-master/*.cpp" }
	files { "E:/SDKs/imgui-master/backends/imgui_impl_dx9.cpp" }
	files { "E:/SDKs/imgui-master/backends/imgui_impl_win32.cpp" }
   
	includedirs { "external/injector/include" }
	includedirs { "external" }
	includedirs { "resources" }
	includedirs { "shaders" }
	includedirs { "src" }
	includedirs { "src/rw" }
	includedirs { "src/entities" }
	includedirs { "src/extras" }
	includedirs { "src/render" }
	includedirs { "src/Core" }
	includedirs { "E:/SDKs/imgui-master" }
	includedirs { "E:/SDKs/imgui-master/backends" }
	includedirs { "E:/SDKs/plugin-sdk-master/plugin_sa/game_sa" }
	includedirs { "E:/SDKs/plugin-sdk-master/shared" }
	includedirs { "E:/SDKs/Microsoft DirectX SDK (June 2010)/Include" }
	libdirs { "E:/SDKs/Microsoft DirectX SDK (June 2010)/Lib/x86" }
	includedirs { os.getenv("RWSDK36") }
   
	prebuildcommands {
		-- Stochastic shaders (ps_2_a)
		"for /r \"../shaders/ps/2_a/\" %%f in (*.hlsl) do \"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T ps_3_0 /nologo /E main /Fo ../resources/cso/%%~nf.cso %%f",
		-- Bundled building PS
		"\"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T ps_3_0 /nologo /E main_simple /Fo ../resources/cso/buildingPipePS.cso \"../shaders/buildingPipePS.hlsl\"",
		"\"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T vs_3_0 /nologo /E main_vehicle /Fo ../resources/cso/buildingPipeVS.cso \"../shaders/buildingPipeVS.hlsl\"",
		-- Bundled vehicle PS
		"\"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T ps_3_0 /nologo /E main_envCar /Fo ../resources/cso/vehiclePipePS.cso \"../shaders/vehiclePipePS.hlsl\"",
		"\"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T vs_3_0 /nologo /E main_vehicle /Fo ../resources/cso/vehiclePipeVS.cso \"../shaders/vehiclePipeVS.hlsl\"",
		-- All remaining PS shaders
		"for /r \"../shaders/ps/\" %%f in (*.hlsl) do \"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T ps_3_0 /nologo /E main /Fo ../resources/cso/%%~nf.cso %%f",
		-- All remaining VS shaders
		"for /r \"../shaders/vs/\" %%f in (*.hlsl) do \"%DXSDK_DIR%\\Utilities\\bin\\x86\\fxc.exe\" /T vs_3_0 /nologo /E main /Fo ../resources/cso/%%~nf.cso %%f",
	}
      
project "skygfx"
	kind "SharedLib"
	language "C++"
	targetdir "bin/%{cfg.buildcfg}"
	targetextension ".dll"
	characterset ("MBCS")
	systemversion "10.0.26100.0"

	defines { "_CRT_USE_MM_LOADU_SI64=0" }
	buildoptions { "/FS", "/Zc:threadSafeInit-" }

	links { "d3dx9" }

filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"
		flags { "StaticRuntime" }
		links { "DxErr" }
		debugdir "E:/games/gtasa_skygfx_plus"
		debugcommand "E:/games/gtasa_skygfx_plus/gta_sa.exe"
		postbuildcommands "copy /y \"$(TargetPath)\" \"E:\\games\\gtasa_skygfx_plus\\skygfx.asi\""

	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "On"
		flags { "StaticRuntime" }
		debugdir "E:/games/gtasa_skygfx_plus"
		debugcommand "E:/games/gtasa_skygfx_plus/gta_sa.exe"
		postbuildcommands "copy /y \"$(TargetPath)\" \"E:\\games\\gtasa_skygfx_plus\\skygfx.asi\""
