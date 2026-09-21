#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform sampler2D Source;
layout(set=0,binding=1,rgba8) uniform writeonly image2D Output;

layout(push_constant) uniform Params {
  vec2 sourceSize;
  vec2 outputSize;
  float sharpen;
} params;

void main() {
  ivec2 id=ivec2(gl_GlobalInvocationID.xy);
  if(id.x>=int(params.outputSize.x)||id.y>=int(params.outputSize.y)) return;
  vec2 uv=(vec2(id)+0.5)/params.outputSize;
  vec2 t=1.0/params.sourceSize;
  vec4 c=textureLod(Source,uv,0);
  vec4 n=textureLod(Source,uv+vec2(0,-t.y),0);
  vec4 s=textureLod(Source,uv+vec2(0, t.y),0);
  vec4 w=textureLod(Source,uv+vec2(-t.x,0),0);
  vec4 e=textureLod(Source,uv+vec2( t.x,0),0);
  vec4 detail=c-(n+s+w+e)*0.25;
  imageStore(Output,id,clamp(c+detail*params.sharpen,0.0,1.0));
}
