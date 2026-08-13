
#include "xgeom_skin_mb_clusters.vert"
#include "xgeom_skin_mb_skinning.vert"
#include "mb_input_definition_position.vert"

//
// Vertex inputs - position-only pass (e.g. shadow map), stream 0 only. Still needs the packed
// bone offset/weight fields: a skinned vertex's position is meaningless without them, even here.
//
layout(location = 0) in ivec3 in_Pos;          // xyz = compressed pos (R16G16B16_SINT)
layout(location = 1) in uvec4 in_PackedLo;     // m_Packed[0..3] (R8G8B8A8_UINT)
layout(location = 2) in uint  in_PackedHi;     // m_Packed[4..5] (R16_UINT)

//
// Gets the vertex local position, deformed by skinning
//
mb_position getVertexLocalPosition()
{
    // Select cluster by push constant index
    ClusterData selectedCluster = cluster[push.clusterIndex];
    const uint  baseBoneIndex   = uint(selectedCluster.posScale.w);

    // Decode compressed position from int16 to [-1,1], then to local (bind pose) space
    const vec3 norm_pos = (vec3(in_Pos) + 32768.0) / 32767.5 - 1.0;
    const vec4 bindPos  = vec4(norm_pos.xyz * selectedCluster.posScale.xyz + selectedCluster.posTranslation.xyz, 1.0);

    const uint lo32 = in_PackedLo.x | (in_PackedLo.y << 8) | (in_PackedLo.z << 16) | (in_PackedLo.w << 24);
    const mat4 Skin = ComputeSkinMatrix(lo32, in_PackedHi, baseBoneIndex, push.maxInfluences);

    mb_position Position;
    Position.Value = Skin * bindPos;

    return Position;
}
