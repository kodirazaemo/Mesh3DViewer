cbuffer FrameConstants : register(b0)
{
    float4x4 mvp;
    float4x4 world;
    float4 lightDirection; // xyz: world-space direction toward the light
    float useNormalMap; // 1 when the material has a tangent-space normal texture
};

Texture2D baseColorTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState baseColorSampler : register(s0);

struct VsInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT; // xyz: tangent, w: bitangent handedness
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VsOutput VSMain(VsInput input)
{
    VsOutput output;
    float4 position = float4(input.position, 1.0);
    output.position = mul(mvp, position);
    output.normal = mul(world, float4(input.normal, 0.0)).xyz;
    output.tangent.xyz = mul(world, float4(input.tangent.xyz, 0.0)).xyz;
    output.tangent.w = input.tangent.w;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float3 ShadingNormal(VsOutput input)
{
    float3 normal = normalize(input.normal);
    // No normal map: the directional light uses the vertex normal, as before.
    if (useNormalMap < 0.5)
    {
        return normal;
    }

    float3 tangent = input.tangent.xyz;
    if (dot(tangent, tangent) <= 1.0e-8)
    {
        return normal;
    }
    tangent = normalize(tangent);
    // Drop the part parallel to the normal so a scaled node transform still forms a basis.
    tangent = tangent - normal * dot(normal, tangent);
    if (dot(tangent, tangent) <= 1.0e-8)
    {
        return normal;
    }
    tangent = normalize(tangent);
    float3 bitangent = cross(normal, tangent) * input.tangent.w;
    // UV channel 0 is the channel aiProcess_CalcTangentSpace used. [0, 1] expands to tangent space.
    float3 tangentNormal = normalTexture.Sample(baseColorSampler, input.uv).xyz * 2.0 - 1.0;
    return normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z);
}

float4 PSMain(VsOutput input) : SV_TARGET
{
    float3 normal = ShadingNormal(input);
    float3 light = normalize(lightDirection.xyz);
    float diffuse = saturate(dot(normal, light));
    float3 baseColor = float3(0.72, 0.75, 0.80);
    float3 color = baseColor * (0.20 + 0.80 * diffuse);
    // A missing material texture is a 1x1 white image, which leaves this shading unchanged.
    const float4 sampled = baseColorTexture.Sample(baseColorSampler, input.uv);
    color *= sampled.rgb;
    // Vertex color scales the lit color. White, the missing-channel default, leaves it unchanged.
    color *= input.color.rgb;
    // The swap chain is UNORM, so encode a light gamma for a readable image.
    color = pow(max(color, 0.0), 1.0 / 2.2);
    // Straight alpha: texture alpha times vertex-color alpha. Opaque (1) replaces the target.
    return float4(color, sampled.a * input.color.a);
}
