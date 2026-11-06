// engine/platform/shaders/depthclear.hlsl -- the depth set to the clear
// plane's over a rectangle (the backdrop's front pass clears it over the
// Pokemon's boxes: plat_gl3d.c front_depth_clear, a scissored glClear
// there).  The pipeline writes the depth and the depth target only.  The
// vertex shader is present.hlsl's.

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer FragmentUniforms : register(b0, space3)
{
    float4 p;           // the clear depth
};

struct FSOut
{
    float4 color : SV_Target0;
    float id : SV_Target1;
    float depth : SV_Target2;
    float z : SV_Depth;
};

FSOut fs_main(VSOut i)
{
    FSOut o;
    o.color = float4(0.0, 0.0, 0.0, 0.0);
    o.id = 0.0;
    o.depth = p.x;
    o.z = p.x;
    return o;
}
