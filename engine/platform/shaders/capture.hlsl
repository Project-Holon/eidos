// engine/platform/shaders/capture.hlsl -- the display capture at the 3D's
// resolution (plat_gl.c's k_fsc).  mode 0 blends two frames, (eva A + evb
// B) / 16 a channel, as DISPCAPCNT does; mode 1 shows a frame with the
// master brightness.  Every picture here is top-down (GL flipped its own
// frames).  The vertex shader is present.hlsl's.

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D<float4> A : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState s0 : register(s0, space2);
[[vk::combinedImageSampler]] Texture2D<float4> B : register(t1, space2);
[[vk::combinedImageSampler]] SamplerState s1 : register(s1, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 ev;          // eva, evb, bfac
    int4 m;             // mode, bmode
};

float4 fs_main(VSOut i) : SV_Target0
{
    float3 a = A.Sample(s0, i.uv).rgb;
    if (m.x == 0) {
        float3 b = B.Sample(s1, i.uv).rgb;
        return float4(min(float3(1.0, 1.0, 1.0), a * ev.x + b * ev.y), 1.0);
    }
    if (m.y == 1) a = a + (1.0 - a) * ev.z;
    else if (m.y == 2) a = a * (1.0 - ev.z);
    return float4(a, 1.0);
}
