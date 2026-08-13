#version 450

#include "xgeom_skin_mb_input_position.vert"

// Mesh-level uniforms (updated once per mesh / per frame)
layout(set = 2, binding = 0) uniform MeshUniforms
{
    mat4 L2C;           // Local to clip matrix
} mesh;

// Simple transform
void main()
{
    gl_Position = mesh.L2C * getVertexLocalPosition().Value;
}

