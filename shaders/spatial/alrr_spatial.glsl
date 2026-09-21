// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — ALRR Spatial 0.1 (GLSL 450, Vulkan compute)
// Same algorithm as alrr_spatial.hlsl for the Vulkan backend.
// One invocation per OUTPUT pixel. rgba8 destination image.
// ─────────────────────────────────────────────────────────────────────────────
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D uSrc;
layout(set = 0, binding = 1, rgba8) uniform writeonly image2D uDst;

layout(push_constant) uniform PushConstants
{
    uint  srcWidth;
    uint  srcHeight;
    uint  dstWidth;
    uint  dstHeight;
    float sharpen;      // 0..1
} pc;

vec4 sampleBilinear(vec2 uv)
{
    // uv in SOURCE texel space, center-aligned.
    vec2 xy = clamp(uv, vec2(0.0), vec2(float(pc.srcWidth) - 1.0, float(pc.srcHeight) - 1.0));
    // texture() samples normalized coords; emulate texel fetch + manual lerp
    // to keep corners exact (matching the CPU/HLSL twins).
    vec2 f = floor(xy);
    vec2 t = xy - f;

    ivec2 i00 = ivec2(f);
    ivec2 i10 = ivec2(min(f.x + 1.0, float(pc.srcWidth)  - 1.0), i00.y);
    ivec2 i01 = ivec2(i00.x, min(f.y + 1.0, float(pc.srcHeight) - 1.0));
    ivec2 i11 = ivec2(i10.x, i01.y);

    vec4 p00 = texelFetch(uSrc, i00, 0);
    vec4 p10 = texelFetch(uSrc, i10, 0);
    vec4 p01 = texelFetch(uSrc, i01, 0);
    vec4 p11 = texelFetch(uSrc, i11, 0);

    return mix(mix(p00, p10, t.x), mix(p01, p11, t.x), t.y);
}

void main()
{
    ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    if (dst.x >= int(pc.dstWidth) || dst.y >= int(pc.dstHeight))
        return;

    vec2 srcXY = (vec2(dst) + 0.5) * vec2(float(pc.srcWidth), float(pc.srcHeight))
                                       / vec2(float(pc.dstWidth), float(pc.dstHeight)) - 0.5;

    vec4 center = sampleBilinear(srcXY);

    if (pc.sharpen <= 0.0)
    {
        imageStore(uDst, dst, center);
        return;
    }

    vec3 blur = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
        blur += sampleBilinear(srcXY + vec2(float(dx), float(dy))).rgb;
    blur /= 9.0;

    vec3 outRgb = center.rgb + clamp(pc.sharpen, 0.0, 1.0) * (center.rgb - blur);
    imageStore(uDst, dst, vec4(clamp(outRgb, 0.0, 1.0), center.a));
}
