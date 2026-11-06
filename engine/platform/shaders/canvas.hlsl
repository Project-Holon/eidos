// engine/platform/shaders/canvas.hlsl -- the interface canvas (plat_gl.c's
// k_fsu): straight RGBA, sampled nearest, blended over what is there by the
// pipeline (source alpha).  The vertex shader is present.hlsl's.

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D<float4> ctex : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp : register(s0, space2);

float4 fs_main(VSOut i) : SV_Target0
{
    return ctex.Sample(smp, i.uv);
}
