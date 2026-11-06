// engine/platform/shaders/comp3d.hlsl -- a screen whose 3D layer the GPU
// drew (plat_gl.c's k_fs3; the rule is in its comment).  `tex` is the
// screen composed with the sentinel 3D, `behind` with the 3D layer off,
// `zero` with an opaque black 3D, all at DS size; `g3d` the GPU's
// premultiplied 3D frame at any size, top-down (GL's was bottom-up).  The
// vertex shader is present.hlsl's.

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D<float4> tex : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState s0 : register(s0, space2);
[[vk::combinedImageSampler]] Texture2D<float4> behind : register(t1, space2);
[[vk::combinedImageSampler]] SamplerState s1 : register(s1, space2);
[[vk::combinedImageSampler]] Texture2D<float4> g3d : register(t2, space2);
[[vk::combinedImageSampler]] SamplerState s2 : register(s2, space2);
[[vk::combinedImageSampler]] Texture2D<float4> zero : register(t3, space2);
[[vk::combinedImageSampler]] SamplerState s3 : register(s3, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 sizes;       // texsize, dstsize
    int4 m;             // bmode, dilate, has zero
    float4 f;           // bfac, sentinel r, g, b
};

float3 sharp(Texture2D<float4> t, SamplerState s, float2 uv, float2 texsize, float2 dstsize)
{
    float2 tt = uv * texsize;
    float2 sc = dstsize / texsize;
    if (sc.x >= 1.0 && all(sc == floor(sc)) && sc.x == sc.y)
        return t.Load(int3(clamp(floor(tt), float2(0.0, 0.0), texsize - 1.0), 0)).rgb;
    float2 seam = floor(tt + 0.5);
    float2 p = seam + clamp((tt - seam) * sc, -0.5, 0.5);
    p = lerp(tt, p, step(float2(1.0, 1.0), sc));
    return t.Sample(s, p / texsize).rgb;
}

float4 fs_main(VSOut i) : SV_Target0
{
    float2 texsize = sizes.xy, dstsize = sizes.zw;
    int2 sz = int2(texsize);
    int2 ip = int2(floor(i.uv * texsize));
    bool mk = false;
    int rad = m.y != 0 ? 1 : 0;
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            int2 q = clamp(ip + int2(dx, dy), int2(0, 0), sz - 1);
            if (any(tex.Load(int3(q, 0)).rgb != behind.Load(int3(q, 0)).rgb)) mk = true;
        }
    }
    if (!mk) return float4(sharp(tex, s0, i.uv, texsize, dstsize), 1.0);
    float4 g = g3d.Sample(s2, i.uv);
    float3 P = g.rgb;
    float A = g.a;
    if (m.x == 1) P = (1.0 - f.x) * P + f.x * A;
    else if (m.x == 2) P = (1.0 - f.x) * P;
    float3 b = sharp(behind, s1, i.uv, texsize, dstsize);
    float3 d = P + (1.0 - A) * b;
    if (m.z != 0) {
        int2 c = clamp(ip, int2(0, 0), sz - 1);
        float3 T = tex.Load(int3(c, 0)).rgb;
        float3 X = zero.Load(int3(c, 0)).rgb;
        if (any(T != X)) {
            float w = clamp((T.g - X.g) / f.z, 0.0, 1.0);
            d = min(float3(1.0, 1.0, 1.0), sharp(zero, s3, i.uv, texsize, dstsize) + w * d);
        }
    }
    return float4(d, 1.0);
}
