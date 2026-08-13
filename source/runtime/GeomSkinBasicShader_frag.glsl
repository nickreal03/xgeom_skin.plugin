#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "mb_standard_pbr.frag"
#include "mb_tone_mapper_lion.frag"
#include "mb_lineartogamma.frag"

// binding=0, not 1 like xgeom_static's copy of this shader - that one relies on a 4-sampler pipeline
// layout (shadowmap/normal/albedo/ORME) this v1 doesn't replicate (see file header comment: no
// per-material texture resolution here), so its own single sampler sits at the one slot this
// pipeline actually provides.
layout(binding = 0) uniform sampler2D SamplerDiffuseMap;		// [INPUT_TEXTURE_DIFFUSE]
layout(location = 0) out vec4 outFragColor;

void main()
{
	vec3 FinalColor = PBRLighting
	( vec3(0, 0, 1)
	, texture(SamplerDiffuseMap, In.UV).rgb
	, 1
	, 0.04
	, 1
	, 0
	, vec3(0)
	);


	//
	// Tone map from (HDR) to (LDR) before gamma correction
	// Has a blue tint 
	//
	FinalColor = ToneMapper_lion(FinalColor );


	//
	// Convert to gamma
	//
	outFragColor.a = 1;
	outFragColor.rgb = linearToSrgb(FinalColor);
}

