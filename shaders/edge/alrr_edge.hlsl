// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — ALRR Edge 0.5 WIP (HLSL, SM 5.0 compute)
//
// Edge-adaptive reconstruction, twin of EdgeUpscaler.cpp (CPU reference):
// bilinear resample, then unsharp sharpening MODULATED by the local luma
// gradient — flat areas receive no sharpening (no halos in sky/fog/skin),
// real edges get the full amount.
//
// WIP notes toward the shipping tier:
//  - currently recomputes gradients per output pixel (fine at ≤640×360 src);
//    next: tiny LDS-cached pre-pass over the source, or RG16F gradient texture.
//  - kEdgeLow/kEdgeHigh will become cbuffer parameters for per-game tuning.
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float4> gSrc : register(t0);
RWTexture2D<unorm float4> gDst : register(u0);

cbuffer AlrrEdgeCB : register(b0)
{
    uint  gSrcWidth;
    uint  gSrcHeight;
    uint  gDstWidth;
    uint  gDstHeight;
    float gSharpen;     // 0..1
    float gEdgeLow;     // luma gradient: below → flat (8.0 / 255)
    float gEdgeHigh;    // luma gradient: above → full edge (48.0 / 255)
    float gPad0;
};

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float4 SampleBilinear(float2 uv)
{
    float2 xy = clamp(uv, float2(0.0, 0.0), float2(gSrcWidth - 1, gSrcHeight - 1));
    float2 f  = floor(xy);
    float2 t  = xy - f;
    uint x0 = (uint)f.x;                 uint x1 = min(x0 + 1, gSrcWidth - 1);
    uint y0 = (uint)f.y;                 uint y1 = min(y0 + 1, gSrcHeight - 1);
    return lerp(lerp(gSrc[uint2(x0, y0)], gSrc[uint2(x1, y0)], t.x),
                lerp(gSrc[uint2(x0, y1)], gSrc[uint2(x1, y1)], t.x), t.y);
}

// Luma gradient magnitude at a source-texel position (central differences).
float GradientAt(float2 uv)
{
    float gx = Luma(SampleBilinear(uv + float2(1, 0)).rgb)
             - Luma(SampleBilinear(uv - float2(1, 0)).rgb);
    float gy = Luma(SampleBilinear(uv + float2(0, 1)).rgb)
             - Luma(SampleBilinear(uv - float2(0, 1)).rgb);
    return length(float2(gx, gy)) * 0.5;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gDstWidth || dtid.y >= gDstHeight)
        return;

    float2 srcXY = (float2(dtid.xy) + 0.5) * float2(gSrcWidth, gSrcHeight)
                                             / float2(gDstWidth, gDstHeight) - 0.5;

    float4 center = SampleBilinear(srcXY);

    float g = GradientAt(srcXY);
    float weight = saturate((g - gEdgeLow) / max(gEdgeHigh - gEdgeLow, 1e-5));
    float amount = saturate(gSharpen) * weight;

    if (amount <= 0.0)
    {
        gDst[dtid.xy] = center;
        return;
    }

    float3 blur = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
        blur += SampleBilinear(srcXY + float2(dx, dy)).rgb;
    blur /= 9.0;

    float3 outRgb = center.rgb + amount * (center.rgb - blur);
    gDst[dtid.xy] = float4(saturate(outRgb), center.a);
}
