// engine/platform/shaders/blur.hlsl -- the world behind a modal (plat_gl.c's
// k_fsb): a separable Gaussian-ish blur, 13 taps along `dir` (weights
// exp(-i^2 / 18)), times `tint`.  Run twice: horizontal into a texture, then
// vertical onto the window with the dark tint; with dir 0 it copies.  The
// vertex shader is present.hlsl's.

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D<float4> src : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp : register(s0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float2 win;             // the target's size in pixels
    float2 dir;             // one tap's step, in pixels
    float tint;
};

float4 fs_main(VSOut i) : SV_Target0
{
    float2 q = i.pos.xy / win;
    float3 sum = float3(0.0, 0.0, 0.0);
    float wsum = 0.0;
    for (int k = -6; k <= 6; k++) {
        float w = exp(-float(k * k) / 18.0);
        sum += w * src.Sample(smp, q + dir * float(k) / win).rgb;
        wsum += w;
    }
    return float4(tint * sum / wsum, 1.0);
}
