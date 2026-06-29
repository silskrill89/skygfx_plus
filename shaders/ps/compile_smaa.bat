@echo off
cd /d "E:\dev(dave)\skygfx_plus_expIV\shaders\ps"
echo Compiling SMAA_Edge.hlsl
"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe" /Tps_3_0 /Emain /Fo"..\..\resources\cso\SMAA_Edge.cso" "SMAA_Edge.hlsl"
echo Compiling SMAA_BlendWeight.hlsl
"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe" /Tps_3_0 /Emain /Fo"..\..\resources\cso\SMAA_BlendWeight.cso" "SMAA_BlendWeight.hlsl"
echo Compiling SMAA_BlendNeighbor.hlsl
"C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)\Utilities\bin\x86\fxc.exe" /Tps_3_0 /Emain /Fo"..\..\resources\cso\SMAA_BlendNeighbor.cso" "SMAA_BlendNeighbor.hlsl"
echo Done
