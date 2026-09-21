Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Output : register(u0);
SamplerState LinearClamp : register(s0);

cbuffer AlrrParams : register(b0) {
  float2 SourceSize;
  float2 OutputSize;
  float Sharpen;
};

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= (uint)OutputSize.x || id.y >= (uint)OutputSize.y) return;

  float2 uv=(float2(id.xy)+0.5)/OutputSize;
  float2 texel=1.0/SourceSize;
  float4 c=Source.SampleLevel(LinearClamp,uv,0);
  float4 n=Source.SampleLevel(LinearClamp,uv+float2(0,-texel.y),0);
  float4 s=Source.SampleLevel(LinearClamp,uv+float2(0, texel.y),0);
  float4 w=Source.SampleLevel(LinearClamp,uv+float2(-texel.x,0),0);
  float4 e=Source.SampleLevel(LinearClamp,uv+float2( texel.x,0),0);

  float4 detail=c-(n+s+w+e)*0.25;
  Output[id.xy]=saturate(c+detail*Sharpen);
}
