// engine/platform/shaders/edge.hlsl -- edge marking over the frame just
// drawn (plat_gl3d.c's k_vse and k_fse; the rules are in plat_gl3d.c's
// file comment).  A pixel off the frame counts as the clear plane: its ID
// and depth.  The ID target is R8 UNORM (the value over 255).

struct VSOut
{
    float4 pos : SV_Position;
};

VSOut vs_main(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[[vk::combinedImageSampler]] Texture2D<float4> col : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp0 : register(s0, space2);
[[vk::combinedImageSampler]] Texture2D<float> idt : register(t1, space2);
[[vk::combinedImageSampler]] SamplerState smp1 : register(s1, space2);
[[vk::combinedImageSampler]] Texture2D<float> dep : register(t2, space2);
[[vk::combinedImageSampler]] SamplerState smp2 : register(s2, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    int4 p0;            // step, variant, clear ID
    float4 p1;          // clear depth
    float4 edge[8];     // EDGE_COLOR per 8 IDs
};

static int2 sz;
static const int2 N4[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };

uint rawid(int2 q) { return (uint)(idt.Load(int3(q, 0)) * 255.0 + 0.5); }

uint idat(int2 q)
{
    if (q.x >= 0 && q.y >= 0 && q.x < sz.x && q.y < sz.y) return rawid(q) & 0x3Fu;
    return (uint)p0.z;
}

float ddat(int2 q)
{
    if (q.x >= 0 && q.y >= 0 && q.x < sz.x && q.y < sz.y) return dep.Load(int3(q, 0));
    return p1.x;
}

bool opaque(int2 q)
{
    return q.x >= 0 && q.y >= 0 && q.x < sz.x && q.y < sz.y && (rawid(q) & 0x80u) != 0u;
}

// q (ID qid) touches a farther pixel of another ID
bool front(int2 q, uint qid)
{
    float qd = ddat(q);
    for (int k = 0; k < 4; k++) {
        if (idat(q + N4[k]) != qid && qd < ddat(q + N4[k])) return true;
    }
    return false;
}

// q touches a nearer opaque pixel of another ID: that ID, else 64
uint behind(int2 q)
{
    uint qid = idat(q);
    float qd = ddat(q);
    for (int k = 0; k < 4; k++) {
        int2 n = q + N4[k];
        if (opaque(n) && idat(n) != qid && ddat(n) < qd) return idat(n);
    }
    return 64u;
}

bool near_other(int2 p, uint id, int r)
{
    for (int i = 0; i < 16; i++) {
        float2 d = float2(cos(0.39269908 * float(i)), sin(0.39269908 * float(i)));
        if (idat(p + int2(round(d * float(r + 1)))) != id
            || idat(p + int2(round(d * 0.5 * float(r + 1)))) != id) return true;
    }
    return false;
}

float4 mark(float4 o, uint id, float a)
{
    return float4(lerp(o.rgb, edge[id >> 3].rgb, a), max(o.a, a));
}

float4 fs_main(VSOut i) : SV_Target0
{
    int2 p = int2(i.pos.xy);
    uint w, h;
    col.GetDimensions(w, h);
    sz = int2(w, h);
    float4 o = col.Load(int3(p, 0));
    uint raw = rawid(p);
    uint id = raw & 0x3Fu;
    int step = p0.x, variant = p0.y;
    int r = step - 1;
    if (variant == 2 || variant == 6) {
        uint f = behind(p);
        if (f < 64u) return mark(o, f, 1.0);
        if (variant == 2 || r <= 0 || !near_other(p, id, r)) return o;
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int2 q = p + int2(dx, dy);
                if (dx * dx + dy * dy > r * r || idat(q) != id) continue;
                f = behind(q);
                if (f < 64u) return mark(o, f, 1.0);
            }
        }
        return o;
    }
    if ((raw & 0x80u) == 0u) return o;
    if (variant == 4) {
        int half_ = step / 2;
        for (int k = 0; k < 4; k++) {
            int j = 1;
            bool found = false;
            for (; j <= step; j++) {
                if (idat(p + N4[k] * j) != id) { found = true; break; }
            }
            if (!found) continue;
            int2 bi = p + N4[k] * (j - 1);
            int2 aa = clamp(bi - N4[k] * half_, int2(0, 0), sz - 1);
            if (idat(aa) != id) aa = bi;
            int2 bb = bi + N4[k] * (step - half_);
            if (idat(bb) != id && ddat(aa) < ddat(bb)) return mark(o, id, 1.0);
        }
        return o;
    }
    if (front(p, id)) return mark(o, id, variant == 7 ? 0.5 : 1.0);
    if (variant == 1 || variant == 7 || r <= 0 || !near_other(p, id, r)) return o;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int2 q = p + int2(dx, dy);
            if (dx * dx + dy * dy > r * r || idat(q) != id) continue;
            if (front(q, id)) return mark(o, id, 1.0);
        }
    }
    return o;
}
