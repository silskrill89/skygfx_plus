// NormalBuffer.hlsl - Depth-based normal reconstruction (ps_3_0)
// Reconstructs world-space normals from the depth buffer using screen-space
// derivatives (5-tap cross pattern). Output: RGB=normal [0,1], A=depth
//
// Replaces broken stereo disparity approach that required a second camera.
//
// c0 = (nearClip, farClip, 0, 0)
// c1 = (screenW, screenH, 1/screenW, 1/screenH)
// c2 = (viewToClip00, viewToClip11, 0, 0) — projection matrix diagonal
// s0 = depth buffer (INTZ or D24S8)

sampler2D depthTex : register(s0);

float4 DepthParams  : register(c0);  // (near, far, 0, 0)
float4 ScreenParams : register(c1);  // (w, h, 1/w, 1/h)
float4 ProjDiag     : register(c2);  // (proj[0][0], proj[1][1], 0, 0)

struct PS_INPUT {
    float4 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Convert raw depth buffer value to linear eye-space depth
float LinearDepth(float2 uv)
{
    float z = tex2D(depthTex, uv).r;
    float near = DepthParams.x;
    float far = DepthParams.y;
    // D3D9 perspective: z = (far / (far - near)) * (1 - near/eyeZ)
    // => eyeZ = near * far / (far - z * (far - near))
    return near * far / (max(far - z * (far - near), 1e-7));
}

// Reconstruct view-space XY from UV + linear depth
float2 ViewPosFromUV(float2 uv, float eyeZ)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // D3D9 Y is flipped
    return ndc * eyeZ * ProjDiag.xy;
}

float4 main(PS_INPUT IN) : COLOR
{
    float2 texel = ScreenParams.zw; // 1/screenW, 1/screenH
    float2 uv = IN.TexCoord;

    // 5-tap cross: center + 4 neighbors
    float dc = LinearDepth(uv);

    // Sky pixel → flat normal pointing up
    if(dc >= DepthParams.y * 0.99){
        return float4(0.5, 0.5, 1.0, 1.0);
    }

    float dl = LinearDepth(uv - float2(texel.x, 0));
    float dr = LinearDepth(uv + float2(texel.x, 0));
    float du = LinearDepth(uv - float2(0, texel.y));
    float dd = LinearDepth(uv + float2(0, texel.y));

    // Reconstruct view-space positions
    float3 pc = float3(ViewPosFromUV(uv, dc), dc);
    float3 pl = float3(ViewPosFromUV(uv - float2(texel.x, 0), dl), dl);
    float3 pr = float3(ViewPosFromUV(uv + float2(texel.x, 0), dr), dr);
    float3 pu = float3(ViewPosFromUV(uv - float2(0, texel.y), du), du);
    float3 pd = float3(ViewPosFromUV(uv + float2(0, texel.y), dd), dd);

    // Cross-pair differences for robust normal estimation
    float3 dx1 = pr - pc;
    float3 dx2 = pc - pl;
    float3 dy1 = pd - pc;
    float3 dy2 = pc - pu;

    // Use the pair with smaller depth discontinuity (bilateral weight)
    float edgeH = abs(dl - dc) + abs(dr - dc);
    float edgeV = abs(du - dc) + abs(dd - dc);

    float3 dx = (abs(dx1.z) < abs(dx2.z)) ? dx1 : dx2;
    float3 dy = (abs(dy1.z) < abs(dy2.z)) ? dy1 : dy2;

    float3 normal = normalize(cross(dx, dy) + 1e-7);

    // Normal is in view space. We need world space for the vehicle/building shaders.
    // However, the vehicle PBR shader blends this with its own geometric normal,
    // and view-space normals work well for edge detection and screen-space effects.
    // For now, output view-space normal remapped to [0,1].
    // TODO: multiply by inverse view matrix for true world-space normals.

    float3 encoded = normal * 0.5 + 0.5;

    return float4(encoded, dc / DepthParams.y); // depth normalized to [0,1]
}
