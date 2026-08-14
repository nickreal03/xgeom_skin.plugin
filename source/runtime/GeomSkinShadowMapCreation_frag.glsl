#version 450

// Depth-only - this stage writes nothing, but xGPU requires every pipeline stage to declare an
// IDENTICAL push_constant struct or the pipeline silently rasterizes nothing at all. Must match
// xgeom_skin_mb_clusters.vert's PushConstants block exactly (pulled into the vertex shader via
// xgeom_skin_mb_input_position.vert's #include chain).
layout(push_constant) uniform PushConstants
{
    uint    clusterIndex;
    uint    maxInfluences;
} push;

void main()
{
}


