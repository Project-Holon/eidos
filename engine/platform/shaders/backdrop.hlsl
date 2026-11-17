// engine/platform/shaders/backdrop.hlsl -- the battle backdrop: a placed map
// scene drawn with the battle's camera before the game's polygons
// (plat_gl3d.c's k_vsb and k_fsb, docs/battle-backdrop.md).  Three targets
// as g3d.hlsl's: the colour, the ID (the pipeline does not write it) and
// the depth.

struct VSIn
{
    float3 pos : TEXCOORD0;
    float2 st : TEXCOORD1;
    float4 rgb : TEXCOORD2;     // unsigned bytes, normalised
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 st : TEXCOORD0;
    float3 rgb : TEXCOORD1;
    float3 wpos : TEXCOORD2;
    float tag : TEXCOORD3;      // a tree's tag (bmap_place.c find_trees; 0: none)
};

cbuffer VertexUniforms : register(b0, space1)
{
    float4 r[4];        // clip = rows dot (x, y, z, 1)
    float4 eye;
    float4 pivot;
    int4 bb;            // billboard: 0 none, 1 facing the camera, 2 upright
    float4 uv0;
    float4 uv1;
};

VSOut vs_main(VSIn i)
{
    VSOut o;
    float3 q = i.pos;
    if (bb.x > 0) {
        // built facing +z around the pivot: turn it toward the camera
        float3 z = eye.xyz - pivot.xyz;
        if (bb.x == 2) z.y = 0.0;
        z = normalize(z);
        float3 x = normalize(cross(float3(0.0, 1.0, 0.0), z));
        float3 y = cross(z, x);
        float3 d = i.pos - pivot.xyz;
        q = pivot.xyz + x * d.x + y * d.y + z * d.z;
    }
    q.y += pivot.w;     // a moving object's rise this frame
    float4 p = float4(q, 1.0);
    o.wpos = q;
    // the draw's texture animation this frame (identity: none)
    o.st = float2(dot(uv0.xyz, float3(i.st, 1.0)), dot(uv1.xyz, float3(i.st, 1.0)));
    o.rgb = i.rgb.rgb;
    o.tag = i.rgb.a * 255.0;
    float4 c = float4(dot(r[0], p), dot(r[1], p), dot(r[2], p), dot(r[3], p));
    o.pos = float4(c.x, c.y, 0.5 * (c.z + c.w), c.w);
    return o;
}

[[vk::combinedImageSampler]] Texture2D<float4> tex : register(t0, space2);
[[vk::combinedImageSampler]] SamplerState smp : register(s0, space2);

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 feye;
    float4 fog;
    float4 fogc;
    int4 flags;         // textured, front pass, Pokemon count, clearance places
    float4 cut;         // alpha, cut, cut_hi, tall grass
    float4 poke[32];
    float4 clr[32];     // the camera's clearance: places the camera passed this shot (x, y, z, radius)
    float4 sky;         // the sky pass: on, the layer's height, (unused), the horizon's bend
    float4 skyu;        // ... the picture's u of a layer point (x, z, 1)
    float4 skyv;        // ... and its v
};

struct FSOut
{
    float4 color : SV_Target0;
    float id : SV_Target1;
    float depth : SV_Target2;
};

FSOut fs_main(VSOut i)
{
    FSOut o;
    if (sky.x > 0.0) {
        // the cloud dome: where this pixel's line of sight meets the layer sky.y over the camera (its horizon
        // sky.w under the level), the picture at that point's place on the map (skyu, skyv) in blocks, fading
        // out toward that horizon
        float3 d = normalize(i.wpos - feye.xyz);
        float e = d.y + sky.w;
        if (e < 0.01) discard;
        uint tw, th, tl;
        tex.GetDimensions(0, tw, th, tl);
        float2 sz = float2((float)tw, (float)th);
        float3 q = float3(feye.xz + d.xz / e * sky.y, 1.0);
        float2 st = frac(float2(dot(skyu.xyz, q), dot(skyv.xyz, q)));
        float4 t = tex.SampleLevel(smp, (floor(st * sz) + 0.5) / sz, 0.0);
        float a = t.a * cut.x * smoothstep(0.01, 0.12, e);
        if (a < 0.004) discard;
        o.color = float4(i.rgb * t.rgb * a, a);
        o.id = 0.0;
        o.depth = i.pos.z;
        return o;
    }
    if (flags.w > 0 && i.tag > 0.5) {
        // a tree the camera's clearance reaches goes whole: an entry with a negative radius names the tree's tag
        // (its y) at its place within its width; the triangles that carry that tag are not drawn.  Its level faces
        // (the flat leaf layers) go whole whatever the width (a wide tree loses only a stretch of its upright cards:
        // the layers cut at that stretch's edge were left floating as bands)
        float3 px = ddx(i.wpos), py = ddy(i.wpos);
        float3 tn = cross(px, py);
        bool level = abs(tn.y) > 0.7 * length(tn);
        // an upright card is cut by whole pictures: what is tested is the whole span of the fragment's own repeat
        // of the picture along the card (its middle cp, half its length sh along sd), so each painted tree goes or
        // stays whole (a forest's wall has a tree a repeat)
        float2 sx = ddx(i.st), sy = ddy(i.st);
        float det = sx.x * sy.y - sy.x * sx.y;
        float3 cp = i.wpos;
        float2 sd = float2(0.0, 0.0);
        float sh = 0.0;
        if (!level && abs(det) > 1e-12) {
            float3 dpdu = (px * sy.y - py * sx.y) / det;
            if (length(dpdu) < 200.0) {
                cp = i.wpos + dpdu * (floor(i.st.x) + 0.5 - i.st.x);
                sh = 0.5 * length(dpdu.xz);
                if (sh > 1e-6) sd = dpdu.xz / (2.0 * sh);
            }
        }
        // (the canopy, tag 255: a wide level sheet of leaves over a forest; it goes round each tree that is out,
        // a little wider than the tree)
        for (int c = 0; c < flags.w; c++) {
            if (i.tag > 254.5 && clr[c].w < 0.0 && length(i.wpos.xz - clr[c].xz) < -clr[c].w + 1.5) discard;
        }
        for (int t = 0; t < flags.w; t++) {
            if (clr[t].w < 0.0 && abs(i.tag - clr[t].y) < 0.5) {
                float2 rel = cp.xz - clr[t].xz;
                float al = dot(rel, sd);
                float d = length(float2(max(abs(al) - sh, 0.0), length(rel - al * sd)));
                if (d < (level ? 40.0 : -clr[t].w)) discard;
            }
        }
    }
    // (not tall grass, cut.w: the camera passes through it; not a tree, which goes whole above or is drawn whole: a
    // ball cut out of it left a hole)
    if (flags.w > 0 && cut.w < 0.5 && i.tag < 0.5) {
        // the camera's clearance: an upright surface inside one of the places the camera passed this shot is not
        // drawn; a floor is kept.  The level top of a body the camera stands in or beside goes with its sides (a
        // pedestal's top was left floating as a sheet): a level face seen nearly edge-on, well over the battle's
        // ground (1.5 units over the Pokemon's middle: no floor a battle stands on)
        float3 nf = cross(ddx(i.wpos), ddy(i.wpos));
        float ln = length(nf);
        float3 mid = float3(0.0, 0.0, 0.0);
        float nm = 0.0;
        for (int p = 0; p < flags.z; p++) {
            if (poke[p].w > 0.0) { mid += poke[p].xyz; nm += 1.0; }
        }
        mid = nm > 0.0 ? mid / nm : clr[0].xyz;
        // (between the camera and the battle: the two on either side of its plane)
        bool between = dot(nf, feye.xyz - i.wpos) * dot(nf, mid - i.wpos) < 0.0;
        // (or seen nearly edge-on: a post's narrow side, a sliver)
        between = between || abs(dot(nf, normalize(feye.xyz - i.wpos))) < 0.35 * ln;
        bool top = ln > 1e-9 && abs(nf.y) / ln >= 0.7 && i.wpos.y > mid.y + 1.5
                   && abs(dot(nf, normalize(feye.xyz - i.wpos))) < 0.35 * ln;
        if ((between && ln > 1e-9 && abs(nf.y) / ln < 0.7) || top) {
            for (int q = 0; q < flags.w; q++) {
                if (clr[q].w > 0.0 && length(i.wpos - clr[q].xyz) < clr[q].w) discard;
            }
        }
    }
    if (flags.y == 1) {
        // the second pass: what of the map stands between the camera and a
        // Pokemon, over the sprite's box, is drawn again over it
        bool keep = false, behind = false;
        float3 f = i.wpos - feye.xyz;
        for (int k = 0; k < flags.z; k++) {
            float3 d = poke[k].xyz - feye.xyz;
            float L = length(d);
            if (L < 1e-3) continue;
            float3 n = d / L;
            float along = dot(f, n);
            if (along < 0.05) continue;
            float3 side = normalize(cross(n, float3(0.0, 1.0, 0.0)));
            float3 up = cross(side, n);
            float3 ov = (f - n * along) * (L / along);
            float sx = dot(ov, side), uy = dot(ov, up);
            float hgt = abs(poke[k].w);
            if (abs(sx) < 0.47 * hgt && uy > -1.0 && uy < hgt) {
                if (poke[k].w < 0.0) behind = true;     // a shield: never covered
                if (along <= L - 0.3) keep = true;
                else if (abs(sx) < 0.25 * hgt && uy < 0.6 * hgt) behind = true;
            }
        }
        if (!keep || behind) discard;
    }
    float4 c = float4(i.rgb, 1.0);
    if (flags.x == 1) c *= tex.Sample(smp, i.st);
    float a = c.a * cut.x;
    if (a < cut.y || a >= cut.z) discard;
    if (cut.y > 0.4) a = 1.0;
    if (fogc.a > 0.0) {
        float ff = clamp(dot(i.wpos, fog.xyz) - fog.w, 0.0, 1.0);
        c.rgb = lerp(c.rgb, fogc.rgb, ff);
    }
    o.color = float4(c.rgb * a, a);
    o.id = 0.0;
    o.depth = i.pos.z;
    return o;
}
