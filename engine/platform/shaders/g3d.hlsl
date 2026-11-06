// engine/platform/shaders/g3d.hlsl -- the DS 3D engine's polygons
// (plat_gl3d.c's k_vs3 and k_fs3, engine/g3d/raster.c rule for rule):
// perspective-correct colour and texture coordinates, nearest texel fetch
// with repeat, flip and clamp, modulate / decal / toon / highlight on 6-bit
// colour and 5-bit alpha, the alpha test, z or w depth, culling on the
// projected winding.  Premultiplied colour out.
//
// Three targets: the colour; the polygon ID beside it (R8 UNORM, the value
// over 255: bit 7 opaque, bit 6 fog, bits 0-5 the ID); the depth as the
// depth buffer has it (R32 float), which the edge and fog passes read.

struct VSIn
{
    float4 pos : TEXCOORD0;     // clip space, z from -w to w (the DS's, GL's)
    float2 st : TEXCOORD1;      // texel coordinates times 16
    float3 rgb : TEXCOORD2;     // 0..255
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 st : TEXCOORD0;
    float3 rgb : TEXCOORD1;
    float cw : TEXCOORD2;       // the clip w, perspective-correct: w at the pixel
};

VSOut vs_main(VSIn i)
{
    VSOut o;
    o.st = i.st;
    o.rgb = i.rgb;
    o.cw = i.pos.w;
    // depth 0..1 from GL's -1..1, so the depth values are GL's
    o.pos = float4(i.pos.x, i.pos.y, 0.5 * (i.pos.z + i.pos.w), i.pos.w);
    return o;
}

[[vk::combinedImageSampler]] Texture2D<float4> tex : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp : register(s0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    int4 a;         // mode (0 modulate, 1 decal, 2 toon), alpha5, textured, pass
    int4 b;         // texsize x, y, alpharef, id
    int4 wrap;      // repeat s, repeat t, flip s, flip t
    int4 c;         // toon, highlight, front, back
    int4 d;         // wbuf
    float4 toontab[32];
};

struct FSOut
{
    float4 color : SV_Target0;
    float id : SV_Target1;
    float depth : SV_Target2;
    float z : SV_Depth;
};

int wrapc(int v, int size, int rep, int flip)
{
    if (size <= 0) return 0;
    if (rep == 0) return clamp(v, 0, size - 1);
    if (flip != 0) {
        int k = v & (size * 2 - 1);
        if (k >= size) k = size * 2 - 1 - k;
        return k;
    }
    return v & (size - 1);
}

int mod6(int a8, int b8)
{
    int a6 = a8 >> 2;
    int b6 = b8 >> 2;
    int r = ((a6 + 1) * (b6 + 1) - 1) >> 6;
    r = min(r, 63);
    return r * 255 / 63;
}

int mod5(int x, int y)
{
    int r = ((x + 1) * (y + 1) - 1) >> 5;
    return min(r, 31);
}

FSOut fs_main(VSOut i, bool front : SV_IsFrontFace)
{
    FSOut o;
    if (front) { if (c.z == 0) discard; }
    else { if (c.w == 0) discard; }
    int3 col3 = clamp(int3(floor(i.rgb + 0.001)), int3(0, 0, 0), int3(255, 255, 255));
    int3 hl = int3(0, 0, 0);
    if (c.x == 1) {
        int3 e = int3(toontab[col3.r >> 3].rgb + 0.5);
        if (c.y == 1) { hl = e; col3.g = col3.r; col3.b = col3.r; }
        else { col3 = e; }
    }
    int a5 = a.y;
    int3 colr = col3;
    if (a.z == 1) {
        int x = wrapc(int(floor(i.st.x / 16.0)), b.x, wrap.x, wrap.z);
        int y = wrapc(int(floor(i.st.y / 16.0)), b.y, wrap.y, wrap.w);
        int4 tc = int4(tex.Load(int3(x, y, 0)) * 255.0 + 0.5);
        if (a.x == 1) {
            int ta = tc.a >> 3;
            colr = (tc.rgb * ta + col3 * (31 - ta)) / 31;
        } else {
            colr = int3(mod6(tc.r, col3.r), mod6(tc.g, col3.g), mod6(tc.b, col3.b));
            a5 = mod5(tc.a >> 3, a.y);
        }
    }
    colr = min(colr + hl, int3(255, 255, 255));
    if (a.w == 1 && a5 < 31) discard;
    if (a.w == 2 && a5 >= 31) discard;
    if (a5 == 0 || a5 <= b.z) discard;
    float al = float(a5) / 31.0;
    o.color = float4(float3(colr) / 255.0 * al, al);
    o.id = float(b.w) / 255.0;
    o.z = d.x == 1 ? clamp(i.cw / 16777216.0, 0.0, 1.0) : i.pos.z;
    o.depth = o.z;
    return o;
}
