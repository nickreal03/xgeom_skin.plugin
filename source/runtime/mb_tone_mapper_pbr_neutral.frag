// Reinhard tone mapper(which divides by color + 1, causing blue tint and desaturation) with 
// Khronos PBR Neutral.This preserves PBR color accuracy up to ~0.76 luminance, applies a gradual 1 / x shoulder 
// for highlights, and adds subtle desaturation only in overbrights—resulting in vibrant mids without tint or washout.
vec3 ToneMapper_pbrNeutral(vec3 c)
{
	float startCompression = 0.76, desaturation = 0.15;
	float mn = min(c.r, min(c.g, c.b)), off = mn < 0.08 ? mn - 6.25 * mn * mn : 0.04;
	c -= off;
	float pk = max(c.r, max(c.g, c.b));
	if (pk < startCompression) return c;
	float d = 1 - startCompression, np = 1 - d * d / (pk + d - startCompression);
	c *= np / pk;
	float g = 1 - 1 / (desaturation * (pk - np) + 1);
	return mix(c, np * vec3(1.0), g);
}
