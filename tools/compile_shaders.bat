@echo off
set FXC="C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe"
set CSO=E:\dev(dave)\skygfx_plus_expIV\resources\cso
set SRC=E:\dev(dave)\skygfx_plus_expIV\shaders

%FXC% /T ps_3_0 /E main /Fo "%CSO%\VehiclePBR_Modern.cso" "%SRC%\ps\VehiclePBR_Modern.hlsl"
echo VehiclePBR_Modern: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\Glass_Vehicle.cso" "%SRC%\ps\Glass_Vehicle.hlsl"
echo Glass_Vehicle: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\CarPaint_Reflections.cso" "%SRC%\ps\CarPaint_Reflections.hlsl"
echo CarPaint_Reflections: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SSAO.cso" "%SRC%\ps\SSAO_ps20.hlsl"
echo SSAO: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SkinEnhance.cso" "%SRC%\ps\SkinEnhance.hlsl"
echo SkinEnhance: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\HairEnhance.cso" "%SRC%\ps\HairEnhance.hlsl"
echo HairEnhance: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SSS_Blur.cso" "%SRC%\ps\SSS_Blur.hlsl"
echo SSS_Blur: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\MotionBlur_Burnout.cso" "%SRC%\ps\MotionBlur_Burnout.hlsl"
echo MotionBlur_Burnout: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\ColorFilter_CrossMix.cso" "%SRC%\ps\ColorFilter_CrossMix.hlsl"
echo ColorFilter_CrossMix: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SMAA_Edge.cso" "%SRC%\ps\SMAA_Edge.hlsl"
echo SMAA_Edge: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SMAA_EdgeMotionDepth.cso" "%SRC%\ps\SMAA_EdgeMotionDepth.hlsl"
echo SMAA_EdgeMotionDepth: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SMAA_BlendWeight.cso" "%SRC%\ps\SMAA_BlendWeight.hlsl"
echo SMAA_BlendWeight: %ERRORLEVEL%

%FXC% /T ps_3_0 /E main /Fo "%CSO%\SMAA_BlendNeighbor.cso" "%SRC%\ps\SMAA_BlendNeighbor.hlsl"
echo SMAA_BlendNeighbor: %ERRORLEVEL%

%FXC% /T vs_3_0 /E main /Fo "%CSO%\EdgeTessellationVS.cso" "%SRC%\vs\EdgeTessellationVS.hlsl"
echo EdgeTessellationVS: %ERRORLEVEL%
