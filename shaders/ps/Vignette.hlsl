// Vignette.hlsl - Parametric radial vignette darkening (ps_3_0)
// Circular / rounded edge falloff with independent intensity, radius, softness
// and shape (roundness) control.
//
// Register map:
//   s0     : color input (scene / current pass).
//   c0     : (intensity, radius, softness, roundness)
//              intensity -- darkening strength at the corners (0..1; 1 = full black)
//              radius    -- inner radius of the clear centre (0..~1.4 in UV-dist)
//              softness  -- transition width beyond the radius (>0)
//              roundness -- shape morph: 0 = perfectly circular falloff,
//                           1 = rectangular / screen-shaped falloff
//   c1     : reserved (unused).
//   c2     : reserved (unused).
//
// Distance is measured in a normalized space where the centre is 0 and the
// screen corner is ~1.41 (sqrt(2)). radius/softness are in those units.

sampler2D colorTex : register(s0);

uniform float4 vigParam : register(c0); // (intensity, radius, softness, roundness)

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT IN) : COLOR
{
    float2 texCoord = IN.texCoord;

    float4 color = tex2D(colorTex, texCoord);

    // --- Unpack parameters ---
    float intensity = vigParam.x;
    float radius    = vigParam.y;
    float softness  = vigParam.z;
    float roundness = vigParam.w;

    // Guards + saturate chain.
    intensity = saturate(intensity);
    radius = max(radius, 0.0);
    softness = max(softness, 1e-7);
    roundness = saturate(roundness);

    // No darkening requested.
    if (intensity <= 0.0)
        return color;

    // --- Radial distance (roundness morphs circle <-> rectangle) ---
    float2 d = (texCoord - 0.5) * 2.0;   // -1..1, corner magnitude ~1.41

    float distCircle = length(d);        // euclidean -> circular falloff
    float distSquare = max(abs(d.x), abs(d.y)); // chebyshev -> boxy falloff

    float dist = lerp(distCircle, distSquare, roundness);

    // --- Falloff: full brightness inside radius, darkened beyond radius+softness ---
    float edge0 = radius;
    float edge1 = radius + softness;     // softness > 0 => no divide/flat risk
    float t = smoothstep(edge0, edge1, dist);

    // 1 inside the clear centre -> (1 - intensity) in the far corners.
    float vig = saturate(1.0 - intensity * t);

    color.rgb *= vig;

    return color;
}
