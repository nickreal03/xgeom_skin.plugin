

layout(binding = 0) uniform sampler2D SamplerShadowMap;        // [INPUT_TEXTURE_SHADOW]

//----------------------------------------------------------------------------------------

int isqr(int a) { return a * a; }

//----------------------------------------------------------------------------------------

float SampleShadowTexture(in const vec4 Coord, in const vec2 off)
{
	float dist = texture(SamplerShadowMap, Coord.xy + off).r;
	return (Coord.w > 0.0 && dist < Coord.z) ? 0.0f : 1.0f;
}

//----------------------------------------------------------------------------------------

float ShadowPCF(in const vec4 UVProjection)
{
	float Shadow = 1.0;
	if (UVProjection.z > -1.0 && UVProjection.z < 1.0)
	{
		const float scale = 1.5;
		const vec2  TexelSize = scale / textureSize(SamplerShadowMap, 0);
		const int	SampleRange = 1;
		const int	SampleTotal = isqr(1 + 2 * SampleRange);

		float		ShadowAcc = 0;
		for (int x = -SampleRange; x <= SampleRange; x++)
		{
			for (int y = -SampleRange; y <= SampleRange; y++)
			{
				ShadowAcc += SampleShadowTexture(UVProjection, vec2(TexelSize.x * x, TexelSize.y * y));
			}
		}

		Shadow = ShadowAcc / SampleTotal;
	}

	return Shadow;
}
