#version 450

#include "xgeom_skin_mb_input_full.vert"
#include "mb_varying_definition_full.glsl"

// Mesh-level uniforms (updated once per mesh / per frame)
layout(set=2, binding = 0) uniform MeshUniforms
{
    mat4 L2w;          // Local space -> camera-centered small world
    mat4 w2C;          // Small world -> clip space (projection * view)
    mat4 L2CShadowT;   // Local space -> shadow texture space
} mesh;


// Fragment shader inputs
layout(location = 0) out VaryingFull Out;


void main()
{
    const mb_full_vertex VertData = getVertexData();

    //
    // Transform to all needed spaces
    //
    Out.wSpacePosition      = mesh.L2w        * VertData.LocalPos;
    Out.ShadowPosition      = mesh.L2CShadowT * VertData.LocalPos;
    gl_Position             = mesh.w2C        * Out.wSpacePosition;

    //
    // Set the UVs
    //
    Out.UV = VertData.UV;

    //
    // Handle the Tangent space transforms for MikkTSpace
    //

    // Extract scales from L2w column lengths (handles non-uniform scales in the L2W)
    // However does not handle skew matrices... not used offen
    vec3 scales = vec3( length(mesh.L2w[0].xyz),
                        length(mesh.L2w[1].xyz),
                        length(mesh.L2w[2].xyz));

    // Scale normals/tangents inversely before forward transform
    const vec3 scaledNormal     = normalize(VertData.Normal.xyz)  / scales;
    const vec3 scaledTangent    = normalize(VertData.Tangent.xyz) / scales;

    const mat3 RotMat           = mat3(mesh.L2w);

    Out.Tangent.xyz             = normalize(RotMat * scaledTangent);
    Out.Tangent.w               = VertData.Tangent.w;                   // Binormal signed
    Out.Normal                  = normalize(RotMat * scaledNormal);

    //
    // Save the vertex color
    //
    Out.VertColor = VertData.VertColor;
}

