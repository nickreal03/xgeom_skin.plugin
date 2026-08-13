
struct mb_full_vertex
{
    vec4 VertColor;         // Vertex color white if none
    vec4 LocalPos;          // In local space ready to be transform
    vec4 Tangent;           // Tangent Local Space in (X,Y,Z) W contains the sign of the binormal
    vec3 Normal;            // Normal Local Space
    vec2 UV;                // Texture Coordinates 
};

