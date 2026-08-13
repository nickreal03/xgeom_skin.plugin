// Optimized Exposure Fusion: Single-scale approx (no texture/mips needed; for full multi-pass pyramid, precompute exposures as textures).
// Production-ready: Tunable exposures/shadows/highlights, sigma for weight sharpness.
// Assumes HDR input computed inline; fast O(1) per pixel.
vec3 ToneMapper_exposure_fusion(vec3 hdr)
{
    const float exposure_mid = 1.0;     // Global exposure
    const float shadows_boost = 2.0;    // Shadow lift
    const float highlights_comp = 0.5;  // Highlight compression
    const float weight_sigma = 4.0;     // Midtone pref sharpness

    // Synthetic exposures
    vec3 E[3];
    E[0] = hdr * (exposure_mid * shadows_boost) / (hdr * (exposure_mid * shadows_boost) + vec3(1.0));  // Reinhard proxy
    E[1] = hdr * exposure_mid / (hdr * exposure_mid + vec3(1.0));
    E[2] = hdr * (exposure_mid * highlights_comp) / (hdr * (exposure_mid * highlights_comp) + vec3(1.0));

    // Weights (midtone Gaussian)
    float weight_sum = 0.0;
    vec3 blended = vec3(0.0);
    for (int i = 0; i < 3; i++) 
    {
        float L = dot(E[i], vec3(0.299, 0.587, 0.114));  // Luma
        float w = exp(-weight_sigma * pow(L - 0.5, 2.0));
        weight_sum += w;
        blended += w * E[i];
    }
    if (weight_sum > 0.001) blended /= weight_sum;

    return blended;
}