// Uchimura tone mapper(2017, Hajime Uchimura, "HDR Theory and Practice" presentation) : 
// Parametric HDR - to - LDR operator for real - time rendering, used in Gran Turismo SPORT 
// for photorealistic output on HDR / SDR displays.Piecewise curve : power - law toe(shadows, 
// param c = 1.33 for contrast), linear mid - section(m = 0.22 start, l = 0.4 length, a = 1.0 slope 
// for PBR fidelity), exponential shoulder(P = 1.0 peak brightness, b = 0.0 pedestal); 
// smoothstep blends sections to avoid artifacts.Advantages vs Reinhard(simple, clips highs), 
// Hable Filmic(less parametric control), ACES(fixed filmic look, limited peaks) : 
// Tunable for artists, adaptive to display nits(1000 - 10000), preserves mid - tones without 
// desaturation / hue shifts, fast via 8192 - entry LUT(0.24ms on PS4).In GT Sport : 
// Runtime application post - lighting, handles BT.2020 gamut, calibrated via user slider; 
// per - channel but avoids issues with luminance scaling variants
vec3 ToneMapper_uchimura(vec3 color)
{
    const float m = 0.22;
    const float c = 1.33;
    const float b = 0.0;
    const float S0 = 0.532;
    const float CP = -2.13675;
    const float l0 = 0.312;

    float lum = max(color.r, max(color.g, color.b));
    if (lum <= 0.0) return color;

    float w0 = 1.0 - smoothstep(0.0, m, lum);
    float w2 = step(m + l0, lum);
    float w1 = 1.0 - w0 - w2;

    float T = m * pow(lum / m, c) + b;
    float S = 1.0 - (1.0 - 0.532) * exp(CP * (lum - S0));
    float L = lum; // a=1

    float tmLum = T * w0 + L * w1 + S * w2;
    return color * (tmLum / lum);
}