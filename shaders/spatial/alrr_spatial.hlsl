// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — ALRR Spatial 0.1 (HLSL, SM 5.0 compute)
//
// Center-aligned bilinear resample + 3×3 unsharp sharpen, single dispatch.
// Twin of src/reconstruction/src/SpatialUpscaler.cpp (CPU reference): corners
// sample exactly, flat areas stay flat, alpha is carried unsharpened.
//
// One thread per OUTPUT pixel. src may be any resolution (incl. downscale).
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float4> gSrc : register(t0);
RWTexture2D<unorm float4> gDst : register(u0);

cbuffer AlrrSpatialCB : register(b0)
{
    uint  gSrcWidth;
    uint  gSrcHeight;
    uint  gDstWidth;
    uint  gDstHeight;
    float gSharpen;     // 0..1
    float gPad0;
    float gPad1;
    float gPad2;
};

float4 SampleBilinear(float2 uv)
{
    // uv in SOURCE texel space, already center-aligned.
    float2 xy = clamp(uv, float2(0.0, 0.0), float2(gSrcWidth - 1, gSrcHeight - 1));
    float2 f  = floor(xy);
    float2 t  = xy - f;

    uint x0 = (uint)f.x;                 uint x1 = min(x0 + 1, gSrcWidth - 1);
    uint y0 = (uint)f.y;                 uint y1 = min(y0 + 1, gSrcHeight - 1);

    float4 p00 = gSrc[uint2(x0, y0)];
    float4 p10 = gSrc[uint2(x1, y0)];
    float4 p01 = gSrc[uint2(x0, y1)];
    float4 p11 = gSrc[uint2(x1, y1)];

    return lerp(lerp(p00, p10, t.x), lerp(p01, p11, t.x), t.y);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDstWidth || dtid.y >= gDstHeight)
        return;

    // Destination center → source texel space (center-aligned mapping).
    float2 srcXY = (float2(dtid.xy) + 0.5) * float2(gSrcWidth, gSrcHeight)
                                             / float2(gDstWidth, gDstHeight) - 0.5;

    float4 center = SampleBilinear(srcXY);

    if (gSharpen <= 0.0)
    {
        gDst[dtid.xy] = center;
        return;
    }

    // High-pass on a 3×3 bilinear-neighborhood average (cheap unsharp).
    float3 blur = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
        blur += SampleBilinear(srcXY + float2(dx, dy)).rgb;
    blur /= 9.0;

    float sharpen = saturate(gSharpen);
    float3 outRgb = center.rgb + sharpen * (center.rgb - blur);
    gDst[dtid.xy] = float4(saturate(outRgb), center.a);
}
