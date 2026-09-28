cbuffer FrameConstants : register(b0)
{
    float4x4 mvp;
    float4x4 world;
    float4 lightDirection; // xyz: world-space direction toward the light
};

struct VsInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VsOutput VSMain(VsInput input)
{
    VsOutput output;
    float4 position = float4(input.position, 1.0);
    output.position = mul(mvp, position);
    output.normal = mul(world, float4(input.normal, 0.0)).xyz;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VsOutput input) : SV_TARGET
{
    float3 normal = normalize(input.normal);
    float3 light = normalize(lightDirection.xyz);
    float diffuse = saturate(dot(normal, light));
    float3 baseColor = float3(0.72, 0.75, 0.80);
    float3 color = baseColor * (0.20 + 0.80 * diffuse);
    // Vertex color scales the lit color. White, the missing-channel default, leaves it unchanged.
    color *= input.color.rgb;
    // The swap chain is UNORM, so encode a light gamma for a readable image.
    color = pow(max(color, 0.0), 1.0 / 2.2);
    // Keep TEXCOORD0 live. clip() is a side effect the compiler cannot drop, and
    // ordinary UVs (including the missing-channel default of 0) stay well above the threshold.
    clip(min(input.uv.x, input.uv.y) + 1.0e10);
    return float4(color, input.color.a);
}
