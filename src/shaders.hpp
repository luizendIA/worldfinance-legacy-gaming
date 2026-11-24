// SPDX-License-Identifier: GPL-3.0-or-later
// Original procedural terrain. All geometry, colours and lighting are generated here.
#pragma once
inline constexpr char shaderSource[] = R"hlsl(
struct Vertex { float4 pos : SV_Position; float2 uv : TEXCOORD; };
Vertex screen(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.pos = float4(v.uv * float2(2,-2) + float2(-1,1), 0, 1);
    return v;
}
float ground(float2 p) {
    return 0.52*sin(p.x*1.21 + p.y*.38) + .33*cos(p.y*1.14 - p.x*.72)
         + .18*sin(2.9*p.x + .4*p.y)*cos(p.y*2.4)
         + .09*cos(5.3*p.y + 1.7*p.x);
}
float4 landscape(Vertex v) : SV_Target {
    float2 uv = v.uv;
    float3 eye = float3(0,1.95,-3.5);
    float3 ray = normalize(float3((uv.x-.5)*1.78, .24-uv.y, 1));
    float3 sky = lerp(float3(.20,.35,.34),float3(.035,.095,.12),saturate((.50-uv.y)*2.8));
    float2 sun = (uv-float2(.74,.17))*float2(1.78,1);
    sky += float3(.60,.39,.12)*(1-smoothstep(.046,.05,length(sun)));
    float t=.10;
    [loop] for(int i=0; i<72; ++i) {
        float3 p=eye+ray*t;
        float h=p.y-ground(p.xz);
        if(h < .014) {
            float dx=ground(p.xz+float2(.014,0))-p.y+h;
            float dz=ground(p.xz+float2(0,.014))-p.y+h;
            float light=saturate(dot(normalize(float3(-dx,.014,-dz)),normalize(float3(.6,1,.55))));
            float2 cell=abs(frac(p.xz*2.5+.5)-.5);
            float wire=1-smoothstep(.01,.035+t*.001,min(cell.x,cell.y));
            float3 land=lerp(float3(.023,.102,.10),float3(.16,.49,.38),light*light);
            land+=wire*float3(.18,.43,.27);
            return float4(lerp(land,sky,saturate(t/30)*.65),1);
        }
        t += max(.025,h*.48);
        if(t>34) break;
    }
    return float4(sky,1);
}
Texture2D layer : register(t0);
SamplerState linearClamp : register(s0);
float4 composite(Vertex v) : SV_Target { return layer.Sample(linearClamp,v.uv); }
)hlsl";
