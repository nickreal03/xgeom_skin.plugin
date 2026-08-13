// Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
// Lottes tone mapper(2016, Timothy Lottes, AMD) : Per - channel filmic operator for HDR - to - LDR in real - time pipelines, 
// optimizing contrast / saturation via power - law curve with toe / shoulder; maps mid - gray 0.18 to 0.267, assumes HDR max 8.0 
// (tunable for displays). Used in Gran Turismo Sport, Unity, GPUOpen; fast(few ops), preserves colors better than Reinhard but can clip highlights.
vec3 ToneMapper_lottes(vec3 x)
{
    const vec3 a = vec3(1.6);
    const vec3 d = vec3(0.977);
    const vec3 hdrMax = vec3(8.0);
    const vec3 midIn = vec3(0.18);
    const vec3 midOut = vec3(0.267);

    const vec3 b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const vec3 c =
        (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);

    return pow(x, a) / (pow(x, a * d) * b + c);
}

// To prevent highlight clipping (loss of detail via hard clamps), we increase hdrMax to 100.0 for softer compression on brighter inputs 
// without exceeding 1.0 soon. Apply to luminance (max RGB) and scale colors to preserve saturation/hue, avoiding per-channel desat/shifts.
vec3 ToneMapper_lottes2(vec3 color) 
{
    const float a = 1.6;
    const float d = 0.977;
    const float hdrMax = 100.0;
    const float midIn = 0.18;
    const float midOut = 0.267;

    const float b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const float c =
        (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);

    float peak = max(color.r, max(color.g, color.b));
    float tonemappedPeak = pow(peak, a) / (pow(peak, a * d) * b + c);
    return (color / peak) * tonemappedPeak;
}