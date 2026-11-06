// engine/platform/shaders/world.hlsl -- the one-screen world: a DS screen
// whose 3D the GPU drew at the window's size (plat_gl.c's k_fsw; the rule
// is in its comment).  The 3D frame is sampled at the window pixel, the
// DS-size images through the DS screen's rectangle `dsr` (window pixels, y
// down).  The 3D frame is top-down (GL's was bottom-up).  The vertex
// shader is present.hlsl's.

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
// the kept picture of the field, the window's size, top-down
[[vk::combinedImageSampler]] Texture2D<float4> held : register(t4, space2);
[[vk::combinedImageSampler]] SamplerState s4 : register(s4, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 dsr;         // the DS screen's rectangle: x, y, w, h
    float4 sz2;         // texsize, win
    int4 m;             // bmode, dilate, has zero, hide 2D
    int4 m2;            // crop, under (beyond the DS screen: the kept picture)
    float4 bd;          // the backdrop colour, bfac in .a
    float4 sent;        // the sentinel colour
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
    float2 texsize = sz2.xy, win = sz2.zw;
    float bfac = bd.a;
    float2 fc = i.pos.xy;                       // window pixel, y down
    float2 duv = (fc - dsr.xy) / dsr.zw;
    // a cropped world's 3D was rendered at the DS rectangle's size
    float4 g = g3d.Sample(s2, m2.x != 0 ? duv : fc / win);
    float3 P = g.rgb;
    float A = g.a;
    if (m.x == 1) P = (1.0 - bfac) * P + bfac * A;
    else if (m.x == 2) P = (1.0 - bfac) * P;
    bool inside = m.w == 0 && duv.x >= 0.0 && duv.y >= 0.0 && duv.x < 1.0 && duv.y < 1.0;
    // a scene drawn for the DS's own frame: nothing outside its rectangle
    if (m2.x != 0 && (duv.x < 0.0 || duv.y < 0.0 || duv.x >= 1.0 || duv.y >= 1.0))
        return float4(0.0, 0.0, 0.0, 1.0);
    // outside the DS rectangle: a black or one-colour layer across the row
    // (a wipe, a fade the game draws with a layer) reaches the window's edge
    if (!inside && m.w == 0 && duv.y >= 0.0 && duv.y < 1.0) {
        int2 sz0 = int2(texsize);
        int2 e = int2(duv.x < 0.0 ? 0 : sz0.x - 1, clamp(int(floor(duv.y * texsize.y)), 0, sz0.y - 1));
        float3 T = tex.Load(int3(e, 0)).rgb;
        int2 eo = int2(sz0.x - 1 - e.x, e.y), ec = int2(sz0.x / 2, e.y);
        bool rowflat = all(T == tex.Load(int3(eo, 0)).rgb) && all(T == tex.Load(int3(ec, 0)).rgb);
        if (all(T == behind.Load(int3(e, 0)).rgb) && (max(T.r, max(T.g, T.b)) < 0.03 || rowflat))
            return float4(T, 1.0);
        if (m.z != 0) {
            float3 X = zero.Load(int3(e, 0)).rgb;
            bool xflat = all(X == zero.Load(int3(eo, 0)).rgb) && all(X == zero.Load(int3(ec, 0)).rgb);
            if (any(T != X) && (max(X.r, max(X.g, X.b)) < 0.03 || xflat)) {
                float w = clamp((T.g - X.g) / sent.g, 0.0, 1.0);
                return float4(X + w * P, 1.0);
            }
        }
    }
    // outside: the 3D over the DS screen's own backdrop colour
    if (!inside) {
        float3 B = bd.rgb;
        if (m2.y != 0) {
            // the kept picture, as dark as the game makes its captured
            // screen for a cut-in (FLD3D_CI_DarkenCapturedScreen halves
            // every colour)
            B = held.Sample(s4, fc / win).rgb * 0.5;
        }
        return float4(P + (1.0 - A) * B, 1.0);
    }
    int2 sz = int2(texsize);
    int2 ip = int2(floor(duv * texsize));
    float2 dstsize = dsr.zw;
    bool mk = false;
    int rad = m.y != 0 ? 1 : 0;
    for (int dy = -rad; dy <= rad; dy++) {
        for (int dx = -rad; dx <= rad; dx++) {
            int2 q = clamp(ip + int2(dx, dy), int2(0, 0), sz - 1);
            if (any(tex.Load(int3(q, 0)).rgb != behind.Load(int3(q, 0)).rgb)) mk = true;
        }
    }
    if (!mk) return float4(sharp(tex, s0, duv, texsize, dstsize), 1.0);
    float3 b = sharp(behind, s1, duv, texsize, dstsize);
    float3 d = P + (1.0 - A) * b;
    if (m.z != 0) {
        int2 c = clamp(ip, int2(0, 0), sz - 1);
        float3 T = tex.Load(int3(c, 0)).rgb;
        float3 X = zero.Load(int3(c, 0)).rgb;
        if (any(T != X)) {
            float w = clamp((T.g - X.g) / sent.g, 0.0, 1.0);
            d = min(float3(1.0, 1.0, 1.0), sharp(zero, s3, duv, texsize, dstsize) + w * d);
        }
    }
    return float4(d, 1.0);
}
