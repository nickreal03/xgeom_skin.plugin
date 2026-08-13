// AgX is an open - source HDR - to - LDR tone mapping operator by Troy Sobotka, designed as a filmic
// alternative to ACES with better color fidelity, reduced highlight desaturation, and balanced mid - tones 
// for realistic rendering.This GLSL code by Benjamin Wrensch(Missing Deadlines, MIT license) provides a 
// minimal polynomial approximation(error 3.67e-06).

vec3 AgxDefaultContrastApprox(vec3 x) 
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 Agx(vec3 val) 
{
    const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    const float min_ev = -12.47393; // log2(pow(2, -10) * 0.18)
    const float max_ev = 4.026069;  // Default: log2(pow(2, 6.5) * 0.18); Optimized for general use (wider range than UE-adjusted 0.526)
    val = agx_mat * val;
    val = clamp(log2(val), min_ev, max_ev);
    val = (val - min_ev) / (max_ev - min_ev);
    return AgxDefaultContrastApprox(val);
}

vec3 AgxEotf(vec3 val)
{
    const mat3 agx_mat_inv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );
    val = agx_mat_inv * val;
    val = pow(val, vec3(2.2)); // Linearize for non-sRGB target; comment if sRGB target
    return val;
}

vec3 AgxLook(vec3 val) {
    const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
    float luma = dot(val, lw);
    vec3 offset = vec3(0.0);
    vec3 slope = vec3(1.0);
    vec3 power = vec3(1.35); // Punchy look (AGX_LOOK 2)
    float sat = 1.4;
    val = pow(val * slope + offset, power);
    return luma + sat * (val - luma);
}

vec3 ToneMapper_ApplyAgX(vec3 color) {
    color = Agx(color);
    color = AgxLook(color);
    color = AgxEotf(color);
    return color;
}
