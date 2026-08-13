//
// Cluster struct
//
struct ClusterData
{
    vec4 posScale;                 // xyz = position scale, w = this cluster's base skeleton bone index
    vec4 posTranslation;           // xyz = position translation, w = trash
    vec4 uvScaleTranslation;       // xy = UV Scale, zw = UV offset
};

//
// Cluster-level uniforms array (bound once, indexed per draw)
//
layout(std430, set = 1, binding = 0) buffer ClusterBuffer
{
    ClusterData cluster[];  // Unsized array of structs
};

//
// Per-bone skinning matrices for the CURRENT pose (World * InvBindPose per bone), computed CPU-side
// once per draw and indexed as BaseBoneIndex + BoneOffset[i] - see xgeom_skin_mb_skinning.vert.
//
layout(std430, set = 1, binding = 1) buffer BoneMatrixBuffer
{
    mat4 boneMatrix[];
};

//
// Push constant for cluster index and skin LOD (updated per draw)
//
layout(push_constant) uniform PushConstants
{
    uint    clusterIndex;   // Index into cluster array
    uint    maxInfluences;  // 1..4 - how many bone weights to actually use (weight-truncation skin LOD)
} push;
