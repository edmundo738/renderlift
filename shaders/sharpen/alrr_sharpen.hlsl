// ─────────────────────────────────────────────────────────────────────────────
// RenderLift — ALRR Sharpen (CAS-lite), HLSL SM 5.0 compute
//
// Standalone sharpening pass for content that is already at output
// resolution (inspired by the negative-lobe idea in FidelityFX CAS, but much
// simpler): 3×3 high-pass clamped so the sharpening lobes never overshoot
// the local min/max neighborhood — detail gain without halo rings.
//
// Use for: UI-less games that don't need it, externally-upscaled frames, or
// per-profile extra sharpening on top of ALRR Spatial/Edge.
// ─────────────────────────────────────────────────────────────────────────────

Texture2D<float4> gSrc : register(t0);
RWTexture2D<unorm float4> gDst : register(u0);

cbuffer AlrrSharpenCB : register(b0)
{
    uint  gWidth;
    uint  gHeight;
    float gAmount;      // 0..1
    float gPad0;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gWidth || dtid.y >= gHeight)
        return;

    int2 c0 = int2(dtid.xy);
    int2 mn = int2(0, 0);
    int2 mx = int2(gWidth - 1, gHeight - 1);

    float4 center = gSrc[c0];
    float3 acc = center.rgb;
    float3 nMin = center.rgb;
    float3 nMax = center.rgb;

    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
    {
        if (dx == 0 && dy == 0) continue;
        float3 s = gSrc[clamp(c0 + int2(dx, dy), mn, mx)].rgb;
        acc  += s;
        nMin = min(nMin, s);
        nMax = max(nMax, s);
    }

    float3 blur = acc / 9.0;
    float3 high = center.rgb - blur;

    // Clamp the sharpened result into the local neighborhood range.
    float amount = saturate(gAmount);
    float3 sharpened = clamp(center.rgb + high * amount, nMin, nMax);

    gDst[dtid.xy] = float4(sharpened, center.a);
}
