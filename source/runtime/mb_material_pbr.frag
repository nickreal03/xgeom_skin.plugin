#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "Cache/Plugins/xgeom_static/Runtime/mb_standard_pbr.frag"
#include "Cache/Plugins/xgeom_static/Runtime/mb_tone_mapper_lion.frag"
#include "Cache/Plugins/xgeom_static/Runtime/mb_lineartogamma.frag"

//layout(binding = 0) uniform sampler2D SamplerShadowMap;        // [INPUT_TEXTURE_100]	// depth system dependent
layout(binding = 1) uniform sampler2D SamplerNormal;           // [INPUT_TEXTURE_110]	// liniar BC5 Tangent space normal
layout(binding = 2) uniform sampler2D SamplerAlbedo;           // [INPUT_TEXTURE_120]	// SRGB decompress by Vulkan, BC1/BC7 albedo
layout(binding = 3) uniform sampler2D SamplerORM;              // [INPUT_TEXTURE_130]	// liniar BC1/BC7 ORM packing (AO in R, Roughness in G, Metalness in B)
layout(binding = 4) uniform sampler2D SamplerEmmisive;         // [INPUT_TEXTURE_140]	// SRGB BC1/BC7 compressed Emissive Color

layout(location = 0) out vec4 outFragColor;

vec3 getBC5Normal( vec2 UV )
{
	Normal.xy = (texture(SamplerNormal, UV).gr * 2.0) - 1.0;
	Normal.z = sqrt(1.0 - dot(Normal.xy, Normal.xy));
	return Normal;
}

void main()
{
	vec3 ORM = texture(SamplerORM, In.UV).rgb
	vec3 FinalColor = PBRLighting
	( getBC5Normal(In.UV))
	, texture(SamplerAlbedo, In.UV).rgb
	, ORM.r
	, getBC5Normal(In.UV)
	, vec3(0.04)					// No texture for now...
	, ORM.g
	, ORM.b
	, texture(SamplerEmmisive, In.UV).rgb
	);
	FinalColor = ToneMapper_lion(FinalColor, 1.0);

	outFragColor.a = 1;
	outFragColor.rgb = linearToSrgb(FinalColor);
}