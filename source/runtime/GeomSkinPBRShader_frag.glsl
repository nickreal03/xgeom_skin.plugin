#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Real PBR variant, paired with a MaterialInstance that resolves to 4 texture slots
// (ShadowMap[unused here]/Normal/Albedo/ORM), matching E21_StaticGeom_Editor's own PBR pipeline
// layout and xmaterial.plugin's mb_material_pbr.glsl. binding=0 (shadow map) is intentionally left
// undeclared - this pass doesn't sample it, same convention mb_material_pbr.glsl itself uses.
#include "mb_standard_pbr.frag"
#include "mb_tone_mapper_lion.frag"
#include "mb_lineartogamma.frag"

layout(binding = 1) uniform sampler2D SamplerNormal;   // linear BC5 tangent-space normal
layout(binding = 2) uniform sampler2D SamplerAlbedo;   // sRGB BC1/BC7 albedo
layout(binding = 3) uniform sampler2D SamplerORM;      // linear BC1/BC7 AO(R)/Roughness(G)/Metalness(B)

layout(location = 0) out vec4 outFragColor;

vec3 getBC5Normal(vec2 UV)
{
	vec3 Normal;
	Normal.xy = (texture(SamplerNormal, UV).gr * 2.0) - 1.0;
	Normal.z  = sqrt(1.0 - dot(Normal.xy, Normal.xy));
	return Normal;
}

void main()
{
	vec4 ORM    = texture(SamplerORM, In.UV);
	vec4 Albedo = texture(SamplerAlbedo, In.UV);

	vec3 FinalColor = PBRLighting
	( getBC5Normal(In.UV)
	, Albedo.rgb
	, ORM.r         // AO
	, 0.04f         // F0 (non-metal dielectric baseline)
	, ORM.g         // Roughness
	, ORM.b         // Metalness
	, vec3(0)       // No emissive map for this character
	);

	FinalColor = ToneMapper_lion(FinalColor);

	outFragColor.a   = 1;
	outFragColor.rgb = linearToSrgb(FinalColor);
}
