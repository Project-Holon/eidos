// engine/platform/shaders/present.hlsl -- the presenter: one DS screen (or
// any picture) drawn into a rectangle of the window with sharp bilinear
// scaling.  The SDL3 GPU backend's version of plat_gl.c's k_vs and k_fs;
// tools/shaders.py compiles it (docs/graphics-backends.md).
//
// SDL3's binding layout: a vertex shader's uniforms are in space1; a
// fragment shader's textures in space2, its uniforms in space3.  A texture
// and its sampler share a binding (combined, as SDL's Vulkan path expects).

cbuffer VertexUniforms : register(b0, space1)
{
    float4 rect;            // x0, y0, x1, y1 in NDC
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID)
{
    VSOut o;
    float2 c = float2(float(id & 1u), float((id >> 1) & 1u));
    o.uv = float2(c.x, 1.0 - c.y);
    o.pos = float4(lerp(rect.xy, rect.zw, c), 0.0, 1.0);
    return o;
}

[[vk::combinedImageSampler]] Texture2D<float4> tex : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp : register(s0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float2 texsize;
    float2 dstsize;
};

// Sharp bilinear (plat_gl.c's SHARP_FN): a texel keeps its own colour except
// across the seam to its neighbour, where the blend is one output pixel wide
// at any scale; at a whole-number scale the texel is read directly.
float3 sharp(float2 uv)
{
    float2 t = uv * texsize;
    float2 sc = dstsize / texsize;
    if (sc.x >= 1.0 && all(sc == floor(sc)) && sc.x == sc.y)
        return tex.Load(int3(clamp(floor(t), float2(0.0, 0.0), texsize - 1.0), 0)).rgb;
    float2 seam = floor(t + 0.5);
    float2 p = seam + clamp((t - seam) * sc, -0.5, 0.5);
    p = lerp(t, p, step(float2(1.0, 1.0), sc));
    return tex.Sample(smp, p / texsize).rgb;
}

float4 fs_main(VSOut i) : SV_Target0
{
    return float4(sharp(i.uv), 1.0);
}
