// Extracting position of objects from depth buffer
////////////////////////////////////////////////////

uniform sampler2D depthTexture : register(s0);
uniform sampler2D sceneSampler : register(s1);
uniform float4x4 g_ViewProjectionInverseMatrix : register(c0);
uniform float4x4 g_previousViewProjectionMatrix : register(c4);
uniform int g_numSamples : register(c8);

struct PS_INPUT
{
    float2 texCoord : TEXCOORD0;
};

float4
main(PS_INPUT IN) : COLOR
{
    float zOverW = tex2D(depthTexture, IN.texCoord);
    float4 H = float4(IN.texCoord.x * 2 - 1, (1 - IN.texCoord.y) * 2 - 1, zOverW, 1);
    float4 D = mul(H, g_ViewProjectionInverseMatrix);
    float4 worldPos = D / D.w;

    float4 currentPos = H;
    float4 previousPos = mul(worldPos, g_previousViewProjectionMatrix);
    float2 velocity = (currentPos - previousPos) * 0.5;

    float4 color = tex2D(sceneSampler, IN.texCoord);
    float2 texCoord = IN.texCoord + velocity;
    
    // Simple fixed-sample motion blur for ps_2_0 compatibility
    float4 sample1 = tex2D(sceneSampler, texCoord);
    texCoord += velocity;
    float4 sample2 = tex2D(sceneSampler, texCoord);
    texCoord += velocity;
    float4 sample3 = tex2D(sceneSampler, texCoord);
    texCoord += velocity;
    float4 sample4 = tex2D(sceneSampler, texCoord);
    texCoord += velocity;
    float4 sample5 = tex2D(sceneSampler, texCoord);

    color += sample1 + sample2 + sample3 + sample4 + sample5;
    float numSamples = 6.0;
    
    float4 finalColor = color / numSamples;
    return finalColor;
}