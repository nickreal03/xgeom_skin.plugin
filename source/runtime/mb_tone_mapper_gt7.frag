// gt7Curve is a GLSL function implementing Gran Turismo 7's V2 tone mapping S-curve for HDR-to-LDR compression. 
// It features a power-law toe (shadows; strength t=1.280) blended via smoothstep to linear mids (up to l=0.444), 
// then exponential shoulder (highlights; params a=0.25, m=0.538, k precomputed). Applied per-channel, 
// followed by UCS (ICtCp/Jzazbz) chroma scaling and 0.6 blend ratio for desaturation without hue twist.

const float m1 = 0.1593017578125;
const float m2 = 78.84375;
const float c1 = 0.8359375;
const float c2 = 18.8515625;
const float c3 = 18.6875;
const float pqMax = 10000.0;
const float refLum = 100.0;

const float peakPhysical = 1000.0;
const float framebufferLuminanceTarget = peakPhysical / refLum;
const float sdrCorrectionFactor = 1.0;
const float blendRatio = 0.6;
const float fadeStart = 0.98;
const float fadeEnd = 1.16;
const float alpha = 0.25;
const float midPoint = 0.538;
const float linearSection = 0.444;
const float toeStrength = 1.280;
const float k = (linearSection - 1.0) / (alpha - 1.0);
const float kA = framebufferLuminanceTarget * linearSection + framebufferLuminanceTarget * k;
const float kB = -framebufferLuminanceTarget * k * exp(linearSection / k);
const float kC = -1.0 / k;

float eotfSt2084(float n) {
    float np = pow(n, 1.0 / m2);
    float l = max(np - c1, 0.0) / (c2 - c3 * np);
    float physical = pow(l, 1.0 / m1) * pqMax;
    return physical / refLum;
}

float inverseEotfSt2084(float v) {
    float physical = v * refLum;
    float y = physical / pqMax;
    float ym = pow(y, m1);
    float num = c1 + c2 * ym;
    float den = 1.0 + c3 * ym;
    return pow(num / den, m2);
}

vec3 rgbToICtCp(vec3 rgb) 
{
    float l = (1688.0 * rgb.r + 2146.0 * rgb.g + 262.0 * rgb.b) / 4096.0;
    float m = (683.0 * rgb.r + 2951.0 * rgb.g + 462.0 * rgb.b) / 4096.0;
    float s = (99.0 * rgb.r + 309.0 * rgb.g + 3688.0 * rgb.b) / 4096.0;
    float lPQ = inverseEotfSt2084(l);
    float mPQ = inverseEotfSt2084(m);
    float sPQ = inverseEotfSt2084(s);
    vec3 ictcp;
    ictcp.x = (2048.0 * lPQ + 2048.0 * mPQ) / 4096.0;
    ictcp.y = (6610.0 * lPQ - 13613.0 * mPQ + 7003.0 * sPQ) / 4096.0;
    ictcp.z = (17933.0 * lPQ - 17390.0 * mPQ - 543.0 * sPQ) / 4096.0;
    return ictcp;
}

vec3 iCtCpToRgb(vec3 ictcp) 
{
    float i = ictcp.x, ct = ictcp.y, cp = ictcp.z;
    float l = i + 0.00860904 * ct + 0.11103 * cp;
    float m = i - 0.00860904 * ct - 0.11103 * cp;
    float s = i + 0.560031 * ct - 0.320627 * cp;
    float lLin = eotfSt2084(l);
    float mLin = eotfSt2084(m);
    float sLin = eotfSt2084(s);
    vec3 rgb;
    rgb.r = max(3.43661 * lLin - 2.50645 * mLin + 0.0698454 * sLin, 0.0);
    rgb.g = max(-0.79133 * lLin + 1.9836 * mLin - 0.192271 * sLin, 0.0);
    rgb.b = max(-0.0259499 * lLin - 0.0989137 * mLin + 1.12486 * sLin, 0.0);
    return rgb;
}

float evaluateCurve(float x) 
{
    if (x < 0.0) return 0.0;
    float weightLinear = smoothstep(0.0, midPoint, x);
    float weightToe = 1.0 - weightLinear;
    float toeMapped = midPoint * pow(x / midPoint, toeStrength);
    float shoulder = kA + kB * exp(x * kC);
    if (x < linearSection * framebufferLuminanceTarget) {
        return weightToe * toeMapped + weightLinear * x;
    }
    else {
        return shoulder;
    }
}

vec3 ToneMapper_gt7ToneMap(vec3 color) 
{
    vec3 ucs = rgbToICtCp(color);
    vec3 skewedRgb = vec3(evaluateCurve(color.r), evaluateCurve(color.g), evaluateCurve(color.b));
    vec3 skewedUcs = rgbToICtCp(skewedRgb);
    float framebufferLuminanceTargetUcs = rgbToICtCp(vec3(framebufferLuminanceTarget)).x;
    float lumRatio = ucs.x / framebufferLuminanceTargetUcs;
    float chromaScale = 1.0 - smoothstep(fadeStart, fadeEnd, lumRatio);
    vec3 scaledUcs = vec3(skewedUcs.x, ucs.y * chromaScale, ucs.z * chromaScale);
    vec3 scaledRgb = iCtCpToRgb(scaledUcs);
    vec3 blended = (1.0 - blendRatio) * skewedRgb + blendRatio * scaledRgb;
    return sdrCorrectionFactor * min(blended, vec3(framebufferLuminanceTarget));
}

