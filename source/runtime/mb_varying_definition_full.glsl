struct VaryingFull
{
    vec4 wSpacePosition;    // Position in small world
    vec4 ShadowPosition;    // Position in shadow texture-clip-space
    vec4 VertColor;         // Color of the vertices
    vec4 Tangent;           // Tangent in world XYZ, Binormal Sign in w
    vec3 Normal;            // Normal in world XYZ
    vec2 UV;                // Final texture coordinates
};
