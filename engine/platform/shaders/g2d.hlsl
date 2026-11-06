// engine/platform/shaders/g2d.hlsl -- the DS 2D compositor (docs/gpu-2d.md):
// plat_gl2d.c's k_fs2d for the SDL3 GPU backend, the same integer steps in
// the same order.  One pass composes one screen from the G2dGpuFrame the CPU
// filled (engine/g2d/g2d_gpu.h): the stack walk front to back, the windows,
// the colour effects and the master brightness.  The vertex shader is
// present.hlsl's, over the whole target.
//
// The frame's bytes come in RGBA8 / R8 UNORM textures and are read back as
// the integers they hold (round(v * 255) is exact), so no integer texture
// format is needed on any of SDL's drivers.  Everything is top-down: target
// row 0, texture row 0 and DS row 0 are the same row (GL's were bottom-up).

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::combinedImageSampler]] Texture2D<float4> vram : register(t0, space2);   // the BG window, 1024 bytes a row
[[vk::combinedImageSampler]] SamplerState s0 : register(s0, space2);
[[vk::combinedImageSampler]] Texture2D<float4> pal : register(t1, space2);    // 256 x 18: BG, OBJ, OBJ ext 0..15
[[vk::combinedImageSampler]] SamplerState s1 : register(s1, space2);
[[vk::combinedImageSampler]] Texture2D<float4> objt : register(t2, space2);   // index, priority, source, flags
[[vk::combinedImageSampler]] SamplerState s2 : register(s2, space2);
[[vk::combinedImageSampler]] Texture2D<float4> l3t : register(t3, space2);
[[vk::combinedImageSampler]] SamplerState s3 : register(s3, space2);
[[vk::combinedImageSampler]] Texture2D<float4> rast : register(t4, space2);   // 256 x 768: the four rasters
[[vk::combinedImageSampler]] SamplerState s4 : register(s4, space2);
[[vk::combinedImageSampler]] Texture2D<float4> g3d : register(t5, space2);    // the GPU's 3D, premultiplied
[[vk::combinedImageSampler]] SamplerState s5 : register(s5, space2);

// The frame's parameters as one int array (plat_sdlgpu2d.c fills it):
//   0 pal_count[18], 18 text[40] (4 x en prio hofs vofs tw th code map char
//   bpp8), 58 aff[26] (2 x en prio bgi tiles map char wrap rx ry pa pb pc
//   pd), 84 ras[12] (4 x en prio bgi), 96 l3[4] (en prio bgi hofs), 100
//   win[15] (w0 en x1 x2 y1 y2 layers, w1 the same, obj en, obj layers,
//   outside), 115 bld[6] (effect first second eva evb evy), 121 vram_size,
//   122 obj_on, 123 mb, 124 g3m, 125 g3sz.xy, 127 osz.xy.
cbuffer FragmentUniforms : register(b0, space3)
{
    int4 U[33];
};

int u(int i) { return U[i >> 2][i & 3]; }

#define PAL_COUNT 0
#define TEXT 18
#define AFF 58
#define RAS 84
#define L3 96
#define WIN 100
#define BLD 115
#define VRAM_SIZE 121
#define OBJ_ON 122
#define MB 123
#define G3M 124
#define G3SZ 125
#define OSZ 127

static int ox, oy;

uint4 rd4(Texture2D<float4> t, int2 p) { return uint4(round(t.Load(int3(p, 0)) * 255.0)); }

int rd8(int a) { return int(round(vram.Load(int3(a & 1023, a >> 10, 0)).r * 255.0)); }
int rd16(int a) { return rd8(a) | (rd8(a + 1) << 8); }
int3 palc(int row, int ci)
{
    if (ci < u(PAL_COUNT + row)) return int3(rd4(pal, int2(ci, row)).rgb);
    return int3(255, 0, 255);
}
int to5(int c) { return (c * 31 + 127) / 255; }
int to8(int c5) { return min(c5, 31) * 255 / 31; }
int c5to6(int c5) { c5 = min(c5, 31); return c5 != 0 ? c5 * 2 + 1 : 0; }
int c8to6(int c8) { return (c8 * 63 + 127) / 255; }
int c6to8(int c6) { return min(c6, 63) * 255 / 63; }

// bg.c g2d_bg_screen_index, the four hardware sizes and 128x128
int screen_index(int x, int y, int code)
{
    if (code == 0) return (x + y * 16) & 0xFFFF;
    if (code == 1 || code == 2) return (x + y * 32) & 0xFFFF;
    if (code == 3) return ((x & 31) + (y + (x & ~31)) * 32) & 0xFFFF;
    if (code == 4) {
        int map = ((((y >> 5) * 2 + (x >> 5)) & 0xFFFF) * 0x400) & 0xFFFF;
        int off = ((x & 31) + (y & 31) * 32) & 0xFFFF;
        return (off + map) & 0xFFFF;
    }
    return 0;
}

// bg.c g2d_bg_sample on a hardware text BG: -1 none, else the palette
// colour index; idx the tile pixel
int text_sample(int n, int sx, int sy, out int idx)
{
    int b = TEXT + n * 10;
    int tw = u(b + 4), th = u(b + 5);
    idx = 0;
    if (tw == 0 || th == 0) return -1;
    int px = (sx + u(b + 2)) & (tw * 8 - 1);
    int py = (sy + u(b + 3)) & (th * 8 - 1);
    int tx = px >> 3, ty = py >> 3;
    int ei = screen_index(tx, ty, u(b + 6));
    if (ei >= tw * th) return -1;
    int e = rd16(u(b + 7) + ei * 2);
    int bpt = u(b + 9) != 0 ? 64 : 32;
    int off = (e & 0x3FF) * bpt;
    int size = u(VRAM_SIZE) - u(b + 8);
    int lx = px & 7, ly = py & 7;
    if (off <= size && bpt <= size - off) {
        if (((e >> 10) & 1) != 0) lx = 7 - lx;
        if (((e >> 11) & 1) != 0) ly = 7 - ly;
        if (bpt == 32) {
            int v = rd8(u(b + 8) + off + ly * 4 + (lx >> 1));
            idx = (lx & 1) != 0 ? (v >> 4) : (v & 15);
        } else {
            idx = rd8(u(b + 8) + off + ly * 8 + lx);
        }
    }
    return (bpt == 32 ? ((e >> 12) & 15) * 16 : 0) + idx;
}

// bg_affine.c g2d_bg_affine_sample
int aff_sample(int n, int sx, int sy, out int idx)
{
    int b = AFF + n * 13;
    int tiles = u(b + 3);
    idx = 0;
    if (tiles == 0) return -1;
    int side = tiles * 8;
    int tx = (u(b + 7) + u(b + 9) * sx + u(b + 10) * sy) >> 8;
    int ty = (u(b + 8) + u(b + 11) * sx + u(b + 12) * sy) >> 8;
    if (u(b + 6) != 0) { tx &= side - 1; ty &= side - 1; }
    else if (tx < 0 || ty < 0 || tx >= side || ty >= side) return -1;
    int ei = (ty >> 3) * tiles + (tx >> 3);
    if (ei >= u(VRAM_SIZE) - u(b + 4)) return -1;
    int tile = rd8(u(b + 4) + ei);
    int off = tile * 64;
    int size = u(VRAM_SIZE) - u(b + 5);
    if (off <= size && 64 <= size - off) idx = rd8(u(b + 5) + off + (ty & 7) * 8 + (tx & 7));
    return idx;
}

// window.c g2d_window_mask
bool win_in(int b, int x, int y)
{
    int x2 = u(WIN + b + 2), y2 = u(WIN + b + 4);
    if (x2 > 256 || x2 < u(WIN + b + 1)) x2 = 256;
    if (y2 > 192 || y2 < u(WIN + b + 3)) y2 = 192;
    return x >= u(WIN + b + 1) && x < x2 && y >= u(WIN + b + 3) && y < y2;
}
int win_mask(int x, int y, bool objwin)
{
    if (u(WIN + 0) == 0 && u(WIN + 6) == 0 && u(WIN + 12) == 0) return 0x3F;
    if (u(WIN + 0) != 0 && win_in(0, x, y)) return u(WIN + 5);
    if (u(WIN + 6) != 0 && win_in(6, x, y)) return u(WIN + 11);
    if (u(WIN + 12) != 0 && objwin) return u(WIN + 13);
    return u(WIN + 14);
}

// the GPU's 3D at the layer's pixel bx (scrolled) of DS row y, straight
uint4 g3fetch(int bx, int y)
{
    int g3m = u(G3M);
    // 3: the sentinel colour (where the 3D shows), 4: opaque black, 5:
    // nothing (the 3D off: BG0 stays the 3D layer, never a text BG)
    if (g3m == 3) return uint4(84u, 168u, 52u, 255u);
    if (g3m == 4) return uint4(0u, 0u, 0u, 255u);
    if (g3m == 5) return uint4(0u, 0u, 0u, 0u);
    int2 g3sz = int2(u(G3SZ), u(G3SZ + 1));
    float4 g;
    if (g3m == 1) {
        int gx = (bx * 2 + 1) * g3sz.x / 512;
        int gy = (y * 2 + 1) * g3sz.y / 384;
        g = g3d.Load(int3(clamp(int2(gx, gy), int2(0, 0), g3sz - 1), 0));
    } else {
        // the output pixel's centre in the layer (scrolled), filtered as
        // the old composite sampled the 3D
        float2 osz = float2(u(OSZ), u(OSZ + 1));
        float fx = (float(bx) + (float(ox) + 0.5) * 256.0 / osz.x - float(ox * 256 / int(osz.x))) / 256.0;
        float fy = (float(oy) + 0.5) / osz.y;
        g = g3d.Sample(s5, float2(fx, fy));
    }
    if (g.a <= 0.0) return uint4(0u, 0u, 0u, 0u);
    float3 c = saturate(g.rgb / g.a);
    return uint4(uint3(round(c * 255.0)), uint(round(g.a * 255.0)));
}

// the stack: up to two samples, front to back
static int3 s_rgb[2];
static int s_bit[2];
static bool s_3d[2];
static int s_a5[2];
static bool s_semi[2];
static int n;

void push(int3 rgb, int bit, bool is3d, int a5, bool semi)
{
    if (n < 2) { s_rgb[n] = rgb; s_bit[n] = bit; s_3d[n] = is3d; s_a5[n] = a5; s_semi[n] = semi; n++; }
}

void walk(int x, int y, int allowed, uint4 o)
{
    n = 0;
    for (int prio = 0; prio < 4 && n < 2; prio++) {
        if ((allowed & 0x10) != 0 && u(OBJ_ON) != 0 && o.r != 0u && int(o.g) == prio) {
            int row = o.b == 0u ? 1 : 1 + int(o.b);
            push(palc(row, int(o.r)), 0x10, false, 0, (o.a & 1u) != 0u);
        }
        for (int bgi = 0; bgi < 8 && n < 2; bgi++) {
            if (bgi < 4 && (allowed & (1 << bgi)) == 0) continue;
            int bit = bgi < 4 ? (1 << bgi) : 0;
            if (u(L3) != 0 && u(L3 + 1) == prio && u(L3 + 2) == bgi) {
                int bx = (x + u(L3 + 3)) & 0x1FF;
                if (bx < 256) {
                    uint4 p = u(G3M) == 0 ? rd4(l3t, int2(bx, y)) : g3fetch(bx, y);
                    if (p.a != 0u) {
                        int a5 = (int(p.a) * 31 + 127) / 255;
                        push(int3(p.rgb), bit, true, a5 != 0 ? a5 : 1, false);
                    }
                }
                continue;
            }
            int r = -1;
            for (int i = 0; i < 4; i++) {
                if (r < 0 && u(RAS + i * 3) != 0 && u(RAS + i * 3 + 1) == prio && u(RAS + i * 3 + 2) == bgi) r = i;
            }
            if (r >= 0) {
                uint4 p = rd4(rast, int2(x, y + r * 192));
                if (p.a != 0u) push(int3(p.rgb), bit, false, 0, false);
                continue;
            }
            if (bgi < 4 && u(TEXT + bgi * 10) != 0 && u(TEXT + bgi * 10 + 1) == prio) {
                int idx;
                int ci = text_sample(bgi, x, y, idx);
                if (ci >= 0 && idx != 0) { push(palc(0, ci), bit, false, 0, false); continue; }
            }
            for (int k = 0; k < 2; k++) {
                if (u(AFF + k * 13) == 0 || u(AFF + k * 13 + 1) != prio || u(AFF + k * 13 + 2) != bgi) continue;
                int idx;
                int ci = aff_sample(k, x, y, idx);
                if (ci >= 0 && idx != 0) { push(palc(0, ci), bit, false, 0, false); break; }
            }
        }
    }
}

int3 stack_rgb(int i, out int bit)
{
    if (i < n) { bit = s_bit[i]; return s_rgb[i]; }
    bit = 0x20;
    return palc(0, 0);
}

int3 blend_alpha(int3 a, int3 b)
{
    int eva = min(u(BLD + 3), 16), evb = min(u(BLD + 4), 16);
    int3 o;
    for (int i = 0; i < 3; i++) o[i] = to8(min((to5(a[i]) * eva + to5(b[i]) * evb) >> 4, 31));
    return o;
}

float4 fs_main(VSOut i) : SV_Target0
{
    ox = int(i.pos.x);
    oy = int(i.pos.y);
    int g3m = u(G3M);
    int x = g3m == 2 ? ox * 256 / u(OSZ) : ox, y = g3m == 2 ? oy * 192 / u(OSZ + 1) : oy;
    uint4 o = rd4(objt, int2(x, y));
    int allowed = win_mask(x, y, (o.a & 2u) != 0u);
    walk(x, y, allowed, o);
    int tbit, bbit;
    int3 top = stack_rgb(0, tbit);
    int3 outc = top;
    bool is3d = n > 0 && s_3d[0];
    // window.c and bg.c blend_pixel, in order
    bool done = false;
    if ((win_mask(x, y, (o.a & 2u) != 0u) & 0x20) == 0) done = true;
    if (!done && is3d) {
        int3 below = stack_rgb(1, bbit);
        if ((bbit & u(BLD + 2)) != 0) {
            done = true;
            if (s_a5[0] < 31) {
                int a5 = s_a5[0];
                for (int k = 0; k < 3; k++) {
                    int c6 = (c8to6(top[k]) * (a5 + 1) + c5to6(to5(below[k])) * (31 - a5)) >> 5;
                    outc[k] = c6to8(c6);
                }
            }
        } else if (g3m == 2 && s_a5[0] < 31) {
            // the GPU's smoothed 3D edges over any 2D (the DS has no such
            // partial pixels where the 2D is not a blend target)
            done = true;
            int a5 = s_a5[0];
            for (int k = 0; k < 3; k++) {
                int c6 = (c8to6(top[k]) * (a5 + 1) + c5to6(to5(below[k])) * (31 - a5)) >> 5;
                outc[k] = c6to8(c6);
            }
        }
    }
    if (!done && n > 0 && tbit == 0x10 && s_semi[0]) {
        int3 below = stack_rgb(1, bbit);
        if ((bbit & u(BLD + 2)) != 0) { outc = blend_alpha(top, below); is3d = false; done = true; }
    }
    if (!done && u(BLD) != 0 && (tbit & u(BLD + 1)) != 0) {
        if (u(BLD) == 1) {
            int3 below = stack_rgb(1, bbit);
            if ((bbit & u(BLD + 2)) != 0) { outc = blend_alpha(top, below); is3d = false; }
        } else {
            int evy = min(u(BLD + 5), 16);
            for (int k = 0; k < 3; k++) {
                int a = to5(top[k]);
                int v = u(BLD) == 2 ? a + (((31 - a) * evy) >> 4) : a - ((a * evy) >> 4);
                outc[k] = to8(min(v, 31));
            }
            is3d = false;
        }
    }
    // the master brightness on the 6-bit stage
    int mb = u(MB);
    int mode = (mb >> 14) & 3, f = min(mb & 0x1F, 16);
    if ((mode == 1 || mode == 2) && (mb & 0x1F) != 0) {
        for (int k = 0; k < 3; k++) {
            int c = is3d ? c8to6(outc[k]) : c5to6(to5(outc[k]));
            if (mode == 1) c += ((63 - c) * f) >> 4; else c -= (c * f) >> 4;
            outc[k] = c6to8(c);
        }
    }
    return float4(float3(outc) / 255.0, 1.0);
}
