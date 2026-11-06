// engine/platform/shaders/fog.hlsl -- fog over the frame after the edge
// marking (plat_gl3d.c's k_fsf; raster.c g3d_target_fog is the reference).
// The ID target's bit 6 is the pixel's fog flag; the depth target holds the
// depth in the DS's 15-bit units over 32768.  The colour is premultiplied,
// so the straight colour is taken out, blended and multiplied again; a
// clear-plane pixel of alpha 0 blends from the clear colour.  The vertex
// shader is edge.hlsl's.

struct VSOut
{
    float4 pos : SV_Position;
};

[[vk::combinedImageSampler]] Texture2D<float4> col : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp0 : register(s0, space2);
[[vk::combinedImageSampler]] Texture2D<float> idt : register(t1, space2);
[[vk::combinedImageSampler]] SamplerState smp1 : register(s1, space2);
[[vk::combinedImageSampler]] Texture2D<float> dep : register(t2, space2);
[[vk::combinedImageSampler]] SamplerState smp2 : register(s2, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 fogc;        // FOG_COLOR, alpha in .a
    float4 p0;          // offset, step, alpha only (0 or 1)
    float4 clearc;      // the clear colour
    float4 table[32];   // FOG_TABLE in .x
};

float4 fs_main(VSOut i) : SV_Target0
{
    int2 p = int2(i.pos.xy);
    float4 c = col.Load(int3(p, 0));
    uint id = (uint)(idt.Load(int3(p, 0)) * 255.0 + 0.5);
    if ((id & 0x40u) == 0u) return c;
    float d = min(floor(dep.Load(int3(p, 0)) * 32768.0), 32767.0);
    float offset = p0.x, step = p0.y;
    float den;
    if (step <= 0.0) den = d > offset ? table[31].x : table[0].x;
    else if (d <= offset + step) den = table[0].x;
    else if (d >= offset + step * 32.0) den = table[31].x;
    else {
        float rel = d - offset - step;
        int k = int(floor(rel / step));
        float f = rel - float(k) * step;
        den = floor((table[k].x * (step - f) + table[k + 1].x * f) / step);
    }
    if (den >= 127.0) den = 128.0;
    float3 sc = c.a > 0.0 ? c.rgb / c.a : ((id & 0x80u) == 0u ? clearc.rgb : float3(0.0, 0.0, 0.0));
    float a5 = floor(c.a * 31.0 + 0.5);
    float na = floor((fogc.a * 31.0 * den + a5 * (128.0 - den)) / 128.0) / 31.0;
    float3 nc = p0.z > 0.5 ? sc : (fogc.rgb * den + sc * (128.0 - den)) / 128.0;
    return float4(nc * na, na);
}
