#include "xgeom_skin_compiler.h"

#include <dependencies/xcontainer/source/xcontainer_lockless_pool.h>

#include "dependencies/xraw3d/source/xraw3d.h"
#include "dependencies/xraw3d/source/details/xraw3d_assimp_import_v3.h"

#include "dependencies/meshoptimizer/src/meshoptimizer.h"
#include "dependencies/xbitmap/source/xcolor.h"
#include "dependencies/xbits/source/xbits.h"

#include "../xgeom_skin_descriptor.h"
#include "../xgeom_skin.h"
#include "../xgeom_skin_details.h"

#include "dependencies/xproperty/source/xcore/my_properties.cpp"
#include "dependencies/xmath/source/bridge/xmath_to_xproperty.h"

#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <iostream>

namespace xgeom_skin_compiler
{
    struct implementation : xgeom_skin_compiler::instance
    {
        using geom = xgeom_skin::geom;

        struct vertex
        {
            xmath::fvec3                    m_Position;
            std::array<xmath::fvec2, 4>     m_UVs;
            xcolori                         m_Color;
            xmath::fvec3                    m_Normal;
            xmath::fvec3                    m_Tangent;
            xmath::fvec3                    m_Binormal;

            // Resolved (RAW m_Bone-name -> skeleton bone index) and CleanWeights-sorted/normalized,
            // sorted descending, always summing to 1. Slots beyond what CleanWeights(4,eps) produced
            // are zero-weight padding (m_SkelBoneIndex left at -1, contributes nothing at pack time).
            // Riding along as plain fields here means LOD generation (meshopt_simplify only ever
            // shrinks the index buffer over this unmodified vertex array) carries skin data for free.
            std::array<std::int32_t,4>      m_SkelBoneIndex { -1,-1,-1,-1 };
            std::array<float,4>             m_Weight        { 0,0,0,0 };
        };

        struct lod
        {
            float                           m_ScreenArea;
            std::vector<std::uint32_t>      m_Indices;
        };

        struct sub_mesh
        {
            std::vector<vertex>             m_Vertex;
            std::vector<std::uint32_t>      m_Indices;                      // Actual LOD 0 (Original mesh)
            std::vector<lod>                m_LODs;                         // This is LOD 1..n (New computed LODS)
            std::uint32_t                   m_iMaterial;
            int                             m_nWeights      { 0 };
            int                             m_nUVs          { 0 };
            bool                            m_bHasColor     { false };
            bool                            m_bHasNormal    { false };
            bool                            m_bHasBTN       { false };
        };

        struct mesh
        {
            std::string                     m_Name;
            std::vector<sub_mesh>           m_SubMesh;
        };

        //--------------------------------------------------------------------------------------

        implementation()
        {
            m_FinalGeom.Initialize();
        }

        //--------------------------------------------------------------------------------------

        xerr LoadRaw( const std::wstring_view Path )
        {
            xraw3d::assimp_v3::importer::settings Settings;
            xraw3d::assimp_v3::importer Importer;

            Settings.m_bStaticGeometry = false;   // activates assimp's bone-weight import path
            Settings.m_pGeom = &m_RawGeom;
            Settings.m_pNode = &m_RootNode;

            if ( auto Err = Importer.Import(Path, Settings); Err )
                return xerr::create_f<state, "Failed to import the asset">(Err);

            return {};
        }

        //--------------------------------------------------------------------------------------
        // Built once from the referenced skeleton's manifest (already resolved by the descriptor's
        // own Serialize() override by the time onCompile reads it) - array position in the manifest
        // IS the skeleton bone index, matching xskeleton::skeleton::m_pBones[i] 1:1.

        void BuildBoneNameMap(void)
        {
            m_BoneNameToSkelIndex.clear();
            auto& Bones = m_Descriptor.m_ResolvedSkeletonBones.m_Bones;
            for (int i = 0; i < static_cast<int>(Bones.size()); ++i)
                m_BoneNameToSkelIndex[Bones[i].m_Name] = i;
        }

        //--------------------------------------------------------------------------------------
/*
        void ConvertToCompilerMesh(void)
        {
            for( auto& Mesh : m_RawGeom.m_Mesh )
            {
                auto& NewMesh = m_CompilerMesh.emplace_back();
                NewMesh.m_Name = Mesh.m_Name;
            }

            std::vector<std::int32_t> GeomToCompilerVertMesh( m_RawGeom.m_Facet.size() * 3          );
            std::vector<std::int32_t> MaterialToSubmesh     ( m_RawGeom.m_MaterialInstance.size()   );

            int MinVert     = 0;
            int MaxVert     = int(GeomToCompilerVertMesh.size()-1);
            int CurMaterial = -1;
            int LastMesh    = -1;

            for( auto& Face : m_RawGeom.m_Facet )
            {
                auto& Mesh = m_CompilerMesh[Face.m_iMesh];

                // Make sure all faces are shorted by the mesh
                assert(Face.m_iMesh >= LastMesh);
                LastMesh = Face.m_iMesh;

                // are we dealing with a new mesh? if so we need to reset the remap of the submesh
                if( Mesh.m_SubMesh.size() == 0 )
                {
                    for (auto& R : MaterialToSubmesh) R = -1;
                }

                // are we dealiong with a new submesh is so we need to reset the verts
                if( MaterialToSubmesh[Face.m_iMaterialInstance] == -1 )
                {
                    MaterialToSubmesh[Face.m_iMaterialInstance] = int(Mesh.m_SubMesh.size());
                    auto& Submesh = Mesh.m_SubMesh.emplace_back();

                    Submesh.m_iMaterial = Face.m_iMaterialInstance;

                    for (int i = MinVert; i <= MaxVert; ++i)
                        GeomToCompilerVertMesh[i] = -1;

                    MaxVert = 0;
                    MinVert = int(GeomToCompilerVertMesh.size()-1);
                    CurMaterial = Face.m_iMaterialInstance;
                }
                else
                {
                    // Make sure that faces are shorted by materials
                    assert(CurMaterial >= Face.m_iMaterialInstance );
                }

                auto& SubMesh = Mesh.m_SubMesh[MaterialToSubmesh[Face.m_iMaterialInstance]];

                for( int i=0; i<3; ++i )
                {
                    if( GeomToCompilerVertMesh[Face.m_iVertex[i]] == -1 )
                    {
                        GeomToCompilerVertMesh[Face.m_iVertex[i]] = int(SubMesh.m_Vertex.size());
                        auto& CompilerVert = SubMesh.m_Vertex.emplace_back();
                        auto& RawVert      = m_RawGeom.m_Vertex[Face.m_iVertex[i]];

                        CompilerVert.m_Binormal = RawVert.m_BTN[0].m_Binormal;
                        CompilerVert.m_Tangent  = RawVert.m_BTN[0].m_Tangent;
                        CompilerVert.m_Normal   = RawVert.m_BTN[0].m_Normal;
                        CompilerVert.m_Color    = RawVert.m_Color[0];               // This could be n in the future...
                        CompilerVert.m_Position = RawVert.m_Position;
                        
                        if ( RawVert.m_nTangents ) SubMesh.m_bHasBTN    = true;
                        if ( RawVert.m_nNormals  ) SubMesh.m_bHasNormal = true;
                        if ( RawVert.m_nColors   ) SubMesh.m_bHasColor  = true;

                        if( SubMesh.m_Indices.size() && SubMesh.m_nUVs != 0 && RawVert.m_nUVs < SubMesh.m_nUVs )
                        {
                            static bool bDontShowAgain = true;
                            if (bDontShowAgain)
                            {
                                bDontShowAgain = false;
                                printf("WARNING: Found a vertex with an inconsistent set of uvs (Expecting %d, found %d) MeshName: %s \n"
                                    , SubMesh.m_nUVs
                                    , RawVert.m_nUVs
                                    , Mesh.m_Name.data()
                                );
                            }
                        }
                        else
                        {
                            SubMesh.m_nUVs = RawVert.m_nUVs;
                            SubMesh.m_nUVs = 1; //HACK: Force it for now

                            for (int j = 0; j < RawVert.m_nUVs; ++j)
                                CompilerVert.m_UVs[j] = RawVert.m_UV[j];
                        }
                    }

                    assert( GeomToCompilerVertMesh[Face.m_iVertex[i]] >= 0 );
                    assert( GeomToCompilerVertMesh[Face.m_iVertex[i]] < m_RawGeom.m_Vertex.size() );
                    SubMesh.m_Indices.push_back(GeomToCompilerVertMesh[Face.m_iVertex[i]]);

                    MinVert = std::min( MinVert, (int)Face.m_iVertex[i] );
                    MaxVert = std::max( MaxVert, (int)Face.m_iVertex[i] );
                }
            }
        }
*/

xerr ConvertToCompilerMesh(void)
{
    for (auto& Mesh : m_RawGeom.m_Mesh)
    {
        auto& NewMesh = m_CompilerMesh.emplace_back();
        NewMesh.m_Name = Mesh.m_Name;
    }

    std::vector<std::int32_t> GeomToCompilerVertMesh(m_RawGeom.m_Vertex.size(), -1);
    std::vector<std::int32_t> MaterialToSubmesh(m_RawGeom.m_MaterialInstance.size(), -1);

    int CurMaterial = -1;
    int LastMesh = -1;

    for (auto& Face : m_RawGeom.m_Facet)
    {
        auto& Mesh = m_CompilerMesh[Face.m_iMesh];

        // Make sure all faces are sorted by the mesh
        assert(Face.m_iMesh >= LastMesh);
        LastMesh = Face.m_iMesh;

        // are we dealing with a new mesh? if so we need to reset the remap of the submesh
        if (Mesh.m_SubMesh.size() == 0)
        {
            std::fill(MaterialToSubmesh.begin(), MaterialToSubmesh.end(), -1);
        }

        // are we dealing with a new submesh? if so we need to reset the verts
        if (MaterialToSubmesh[Face.m_iMaterialInstance] == -1)
        {
            MaterialToSubmesh[Face.m_iMaterialInstance] = int(Mesh.m_SubMesh.size());
            auto& Submesh = Mesh.m_SubMesh.emplace_back();

            Submesh.m_iMaterial = Face.m_iMaterialInstance;

            std::fill(GeomToCompilerVertMesh.begin(), GeomToCompilerVertMesh.end(), -1);

            CurMaterial = Face.m_iMaterialInstance;
        }
        else
        {
            // Make sure that faces are sorted by materials
            assert(CurMaterial >= Face.m_iMaterialInstance);
        }

        auto& SubMesh = Mesh.m_SubMesh[MaterialToSubmesh[Face.m_iMaterialInstance]];

        for (int i = 0; i < 3; ++i)
        {
            if (GeomToCompilerVertMesh[Face.m_iVertex[i]] == -1)
            {
                GeomToCompilerVertMesh[Face.m_iVertex[i]] = int(SubMesh.m_Vertex.size());
                auto& CompilerVert = SubMesh.m_Vertex.emplace_back();
                auto& RawVert = m_RawGeom.m_Vertex[Face.m_iVertex[i]];

                CompilerVert.m_Binormal = RawVert.m_BTN[0].m_Binormal;
                CompilerVert.m_Tangent = RawVert.m_BTN[0].m_Tangent;
                CompilerVert.m_Normal = RawVert.m_BTN[0].m_Normal;
                CompilerVert.m_Color = RawVert.m_Color[0];               // This could be n in the future...
                CompilerVert.m_Position = RawVert.m_Position;

                if (RawVert.m_nTangents) SubMesh.m_bHasBTN = true;
                if (RawVert.m_nNormals) SubMesh.m_bHasNormal = true;
                if (RawVert.m_nColors) SubMesh.m_bHasColor = true;

                if (SubMesh.m_Indices.size() && SubMesh.m_nUVs != 0 && RawVert.m_nUVs < SubMesh.m_nUVs)
                {
                    static bool bDontShowAgain = true;
                    if (bDontShowAgain)
                    {
                        bDontShowAgain = false;
                        printf("WARNING: Found a vertex with an inconsistent set of uvs (Expecting %d, found %d) MeshName: %s \n"
                            , SubMesh.m_nUVs
                            , RawVert.m_nUVs
                            , Mesh.m_Name.data()
                        );
                    }
                }
                else
                {
                    SubMesh.m_nUVs = RawVert.m_nUVs;

                    for (int j = 0; j < RawVert.m_nUVs; ++j)
                        CompilerVert.m_UVs[j] = RawVert.m_UV[j];
                }

                // Resolve this vertex's (already CleanWeights-sorted/capped-to-4) raw bone indices
                // to skeleton bone indices by name. Only slots that survived CleanWeights are looked
                // up - a raw bone whose weight got culled away never needs to resolve to anything.
                {
                    const int nWeights = std::min<int>(RawVert.m_nWeights, 4);
                    int32_t   MinBone  = std::numeric_limits<std::int32_t>::max();
                    int32_t   MaxBone  = std::numeric_limits<std::int32_t>::min();

                    for (int j = 0; j < nWeights; ++j)
                    {
                        const auto& RawName = m_RawGeom.m_Bone[RawVert.m_Weight[j].m_iBone].m_Name;
                        const auto  It       = m_BoneNameToSkelIndex.find(RawName);
                        if (It == m_BoneNameToSkelIndex.end())
                        {
                            return xerr::create_f<state, "A skin vertex references bone which is not found in the referenced skeleton">();
                        }

                        CompilerVert.m_SkelBoneIndex[j] = It->second;
                        CompilerVert.m_Weight[j]        = RawVert.m_Weight[j].m_Weight;

                        MinBone = std::min(MinBone, It->second);
                        MaxBone = std::max(MaxBone, It->second);
                    }

                    // Hard-error: no amount of cluster splitting can shrink a SINGLE vertex's own
                    // bone span below the 7-bit per-vertex offset budget (127) - two of its own
                    // influences already sit further apart than that in the skeleton's bone order.
                    if (nWeights > 0 && (MaxBone - MinBone) > xgeom_skin::geom::vertex::max_bone_offset_v)
                    {
                        return xerr::create_f<state, "A skin vertex's own bone weights span more skeleton bones than a single cluster can address - author-time skeleton bone order makes this vertex unrepresentable">();
                    }
                }
            }

            assert(GeomToCompilerVertMesh[Face.m_iVertex[i]] >= 0);
            assert(GeomToCompilerVertMesh[Face.m_iVertex[i]] < m_RawGeom.m_Vertex.size());
            SubMesh.m_Indices.push_back(GeomToCompilerVertMesh[Face.m_iVertex[i]]);
        }
    }

    return {};
}


        //--------------------------------------------------------------------------------------

        void GenenateLODs()
        {
            for (auto& M : m_CompilerMesh)
            {
                auto it = m_NameToDetail.find(M.m_Name);
                if (it == m_NameToDetail.end())
                    continue;

                auto* details = it->second;
                if (details->m_LODs.empty())
                    continue;

                for (auto& S : M.m_SubMesh)
                {
                    std::size_t IndexCount = S.m_Indices.size();

                    for (size_t i = 0; i < details->m_LODs.size(); ++i)
                    {
                        const std::size_t target_index_count = std::size_t(IndexCount * details->m_LODs[i].m_LODReduction + 0.005f) / 3 * 3;
                        const float target_error = 1e-2f;
                        const auto& Source = (S.m_LODs.size()) ? S.m_LODs.back().m_Indices : S.m_Indices;

                        if (Source.size() < target_index_count)
                            break;

                        auto& NewLod = S.m_LODs.emplace_back();

                        NewLod.m_Indices.resize(Source.size());
                        std::size_t new_size = meshopt_simplify(NewLod.m_Indices.data(), Source.data(), Source.size(), &S.m_Vertex[0].m_Position[0], S.m_Vertex.size(), sizeof(vertex), target_index_count, target_error);
                        NewLod.m_Indices.resize(new_size);

                        IndexCount = new_size;
                    }
                }
            }
        }

        //--------------------------------------------------------------------------------------

        static xmath::fvec2 oct_wrap(xmath::fvec2 v) noexcept
        {
            return (xmath::fvec2(1.0f) - xmath::fvec2(v.m_Y, v.m_X).Abs()) * (v.Step({ 0 }) * 2.0f - 1.0f);
        }

        //--------------------------------------------------------------------------------------

        static xmath::fvec2 oct_encode(xmath::fvec3 n) noexcept
        {
            n /= (xmath::Abs(n.m_X) + xmath::Abs(n.m_Y) + xmath::Abs(n.m_Z));
            xmath::fvec2 p = xmath::fvec2(n.m_X, n.m_Y);
            if (n.m_Z < 0.0f) p = oct_wrap(p);
            return p;
        }

        //--------------------------------------------------------------------------------------

        struct BBox3
        {
            xmath::fvec3 m_MinPos = xmath::fvec3(std::numeric_limits<float>::max());
            xmath::fvec3 m_MaxPos = xmath::fvec3(std::numeric_limits<float>::lowest());
            void Update(const xmath::fvec3& p) noexcept
            {
                m_MinPos = m_MinPos.Min(p);
                m_MaxPos = m_MaxPos.Max(p);
            }
            xmath::fbbox to_fbbox() const
            {
                xmath::fbbox bb;
                bb.m_Min = m_MinPos;
                bb.m_Max = m_MaxPos;
                return bb;
            }
        };

        //--------------------------------------------------------------------------------------

        struct BBox2
        {
            xmath::fvec2 m_MinUV = xmath::fvec2(std::numeric_limits<float>::max());
            xmath::fvec2 m_MaxUV = xmath::fvec2(std::numeric_limits<float>::lowest());
            void Update(const xmath::fvec2& uv)
            {
                m_MinUV = m_MinUV.Min(uv);
                m_MaxUV = m_MaxUV.Max(uv);
            }
        };

        //--------------------------------------------------------------------------------------
        // Cluster for recursive splitting (triangle ids)

        struct TriCluster
        {
            std::vector<uint32_t> tri_ids;
        };

        static xmath::fvec3 oct_decode(xmath::fvec2 e)
        {
            e = e * 2.0f - 1.0f;                          // [0,1] -> [-1,1]
            xmath::fvec3 v(e.m_X, e.m_Y, 1.0f - std::abs(e.m_X) - std::abs(e.m_Y));
            if (v.m_Z < 0.0f)
            {
                float temp_x = v.m_X;
                float temp_y = v.m_Y;
                v.m_X = (1.0f - std::abs(temp_y)) * (temp_x >= 0.0f ? 1.0f : -1.0f);
                v.m_Y = (1.0f - std::abs(temp_x)) * (temp_y >= 0.0f ? 1.0f : -1.0f);
            }
            return v.NormalizeSafe();
        }

        //--------------------------------------------------------------------------------------

        // Tracks the [min,max] skeleton bone index touched by a candidate cluster - a cluster can
        // only be accepted (Part A) if this span fits within the 7-bit per-vertex offset budget
        // (127), since every vertex's bone offset is relative to one shared per-cluster base index.
        struct BBoxBone
        {
            std::int32_t m_Min = std::numeric_limits<std::int32_t>::max();
            std::int32_t m_Max = std::numeric_limits<std::int32_t>::min();
            void Update(std::int32_t b) noexcept { if (b < 0) return; m_Min = std::min(m_Min, b); m_Max = std::max(m_Max, b); }
            bool empty() const noexcept { return m_Min > m_Max; }
        };

        static void RecurseClusterSplit
        ( const std::vector<vertex>&        InputVerts
        , const std::vector<uint32_t>&      InputIndices
        , const TriCluster&                 c
        , uint32_t                          MaxVerts
        , const float                       MaxExtent
        , const std::vector<float>&         BinormalSigns
        , std::vector<geom::cluster>&       OutputClusters
        , std::vector<geom::vertex>&        AllStaticVerts
        , std::vector<geom::vertex_extras>& AllExtrasVerts
        , std::vector<geom::cluster_data>&  AllClusterData
        , std::vector<uint32_t>&            AllIndices
        )
        {
            if (c.tri_ids.empty()) return;
            BBox3    bb_pos;
            BBox2    bb_uv;
            BBoxBone bb_bone;

            for (uint32_t ti : c.tri_ids)
            {
                for (int j = 0; j < 3; ++j)
                {
                    uint32_t vi = InputIndices[ti * 3 + j];
                    bb_pos.Update(InputVerts[vi].m_Position);
                    bb_uv.Update(InputVerts[vi].m_UVs[0]);
                    for (int w = 0; w < 4; ++w) bb_bone.Update(InputVerts[vi].m_SkelBoneIndex[w]);
                }
            }

            xmath::fvec3    extent_pos      = bb_pos.m_MaxPos - bb_pos.m_MinPos;
            xmath::fvec2    extent_uv       = bb_uv.m_MaxUV - bb_uv.m_MinUV;
            float           max_e_pos       = std::max({ extent_pos.m_X, extent_pos.m_Y, extent_pos.m_Z });
            float           max_e_uv        = std::max(extent_uv.m_X, extent_uv.m_Y);
            bool            small_extent    = (max_e_pos <= MaxExtent && max_e_uv <= MaxExtent);
            bool            bone_ok         = bb_bone.empty() || (bb_bone.m_Max - bb_bone.m_Min) <= geom::vertex::max_bone_offset_v;

            std::unordered_set<uint32_t> used_verts;
            if (small_extent && bone_ok)
            {
                for (uint32_t ti : c.tri_ids)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        used_verts.insert(InputIndices[ti * 3 + j]);
                    }
                }
            }

            if (small_extent && bone_ok && used_verts.size() <= MaxVerts)
            {
                // Remap verts locally
                std::vector<uint32_t> new_vert_ids(used_verts.begin(), used_verts.end());
                std::sort(new_vert_ids.begin(), new_vert_ids.end());
                std::unordered_map<uint32_t, uint32_t> old_to_new = {};
                uint32_t new_id = 0;
                for (uint32_t ov : new_vert_ids)
                {
                    old_to_new[ov] = new_id++;
                }

                // Use precomputed bboxes
                xmath::fvec3 pos_min    = bb_pos.m_MinPos;
                xmath::fvec3 pos_max    = bb_pos.m_MaxPos;
                xmath::fvec3 pos_center = (pos_min + pos_max) * 0.5f;
                xmath::fvec3 pos_scale  = xmath::fvec3::Max((pos_max - pos_min) * 0.5f, xmath::fvec3(1e-6f));
                xmath::fvec2 uv_min     = bb_uv.m_MinUV;
                xmath::fvec2 uv_max     = bb_uv.m_MaxUV;
                xmath::fvec2 uv_scale   = xmath::fvec2::Max(uv_max - uv_min, xmath::fvec2(1e-6f));

                // Build local indices
                std::vector<unsigned int> local_indices;
                local_indices.reserve(c.tri_ids.size() * 3);
                for (uint32_t ti : c.tri_ids)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        uint32_t ov = InputIndices[ti * 3 + j];
                        local_indices.push_back(old_to_new[ov]);
                    }
                }

                // Optimize vertex cache
                meshopt_optimizeVertexCache(local_indices.data(), local_indices.data(), local_indices.size(), static_cast<unsigned int>(new_vert_ids.size()));

                // Prepare positions for overdraw optimization
                std::vector<float> local_positions(new_vert_ids.size() * 3);
                for (uint32_t i = 0; i < new_vert_ids.size(); ++i)
                {
                    const auto& pos = InputVerts[new_vert_ids[i]].m_Position;
                    local_positions[i * 3 + 0] = pos.m_X;
                    local_positions[i * 3 + 1] = pos.m_Y;
                    local_positions[i * 3 + 2] = pos.m_Z;
                }

                // Optimize overdraw
                meshopt_optimizeOverdraw(local_indices.data(), local_indices.data(), local_indices.size(), local_positions.data(), static_cast<unsigned int>(new_vert_ids.size()), sizeof(float) * 3, 1.05f);

                // Generate fetch remap
                std::vector<unsigned int> fetch_remap(new_vert_ids.size());
                meshopt_optimizeVertexFetchRemap(fetch_remap.data(), local_indices.data(), local_indices.size(), static_cast<unsigned int>(new_vert_ids.size()));

                // Every vertex offsets its bone indices from this SAME cluster-wide base - guaranteed
                // to fit a uint16 (skeletons are nowhere near 65536 bones) and guaranteed by bone_ok
                // above that every vertex's own offsets fit the 7-bit budget.
                const std::int32_t BaseBoneIndex = bb_bone.empty() ? 0 : bb_bone.m_Min;

                // Pack original compressed vertices (fused position+skin) and extras
                std::vector<geom::vertex>           original_static(new_vert_ids.size());
                std::vector<geom::vertex_extras>    original_extras(new_vert_ids.size());
                for (uint32_t i = 0; i < new_vert_ids.size(); ++i)
                {
                    const uint32_t  ov = new_vert_ids[i];
                    const vertex& v = InputVerts[ov];
                    const float     sign_val = BinormalSigns[ov];

                    // Pos compression + fused skin data (Part A)
                    const auto pos = ((v.m_Position - pos_center) / pos_scale + 1.0f) * 32767.5f - 32768.0f;

                    std::array<std::uint8_t,4> BoneOffset{ 0,0,0,0 };
                    for (int w = 0; w < 4; ++w)
                        BoneOffset[w] = (v.m_SkelBoneIndex[w] < 0) ? 0 : static_cast<std::uint8_t>(v.m_SkelBoneIndex[w] - BaseBoneIndex);

                    original_static[i] = geom::vertex::Pack
                    ( static_cast<int16_t>(std::round(pos.m_X))
                    , static_cast<int16_t>(std::round(pos.m_Y))
                    , static_cast<int16_t>(std::round(pos.m_Z))
                    , BoneOffset
                    , v.m_Weight
                    );

                    // UV - unchanged from xgeom_static
                    const auto norm_uv = (v.m_UVs[0] - uv_min) / uv_scale;
                    original_extras[i].m_UV[0] = static_cast<uint16_t>(std::round(norm_uv.m_X * 65535.0f));
                    original_extras[i].m_UV[1] = static_cast<uint16_t>(std::round(norm_uv.m_Y * 65535.0f));

                    // Oct normal - full 16-bit precision now that vertex_extras absorbed m_Extra's
                    // former role and isn't fighting alignment for space (Part A)
                    const auto      oct_n = oct_encode(v.m_Normal.NormalizeSafeCopy());
                    original_extras[i].m_OctNormal[0] = static_cast<uint16_t>(std::round((oct_n.m_X * 0.5f + 0.5f) * 65535.0f));
                    original_extras[i].m_OctNormal[1] = static_cast<uint16_t>(std::round((oct_n.m_Y * 0.5f + 0.5f) * 65535.0f));

                    // Oct tangent - 16 bits X, 15 bits Y + 1 bit binormal sign packed together
                    const auto      oct_t = oct_encode(v.m_Tangent.NormalizeSafeCopy());
                    const uint32_t  t_x   = static_cast<uint32_t>(std::round((oct_t.m_X * 0.5f + 0.5f) * 65535.0f));
                    const uint32_t  t_y   = static_cast<uint32_t>(std::round((oct_t.m_Y * 0.5f + 0.5f) * 32767.0f));
                    const uint32_t  sign_bit = (sign_val < 0.0f ? 1u : 0u);

                    original_extras[i].m_OctTangentX      = static_cast<uint16_t>(t_x);
                    original_extras[i].m_OctTangentY_Sign = static_cast<uint16_t>((t_y & 0x7FFFu) | (sign_bit << 15));
                }

                // Remap vertices and extras
                std::vector<geom::vertex> remapped_static(new_vert_ids.size());
                meshopt_remapVertexBuffer(remapped_static.data(), original_static.data(), new_vert_ids.size(), sizeof(geom::vertex), fetch_remap.data());

                std::vector<geom::vertex_extras> remapped_extras(new_vert_ids.size());
                meshopt_remapVertexBuffer(remapped_extras.data(), original_extras.data(), new_vert_ids.size(), sizeof(geom::vertex_extras), fetch_remap.data());

                // Remap indices
                meshopt_remapIndexBuffer(local_indices.data(), local_indices.data(), local_indices.size(), fetch_remap.data());

                // Append verts to global
                uint32_t cluster_vert_start = static_cast<uint32_t>(AllStaticVerts.size());
                AllStaticVerts.insert(AllStaticVerts.end(), remapped_static.begin(), remapped_static.end());
                AllExtrasVerts.insert(AllExtrasVerts.end(), remapped_extras.begin(), remapped_extras.end());

                // Append indices to global
                uint32_t cluster_index_start = static_cast<uint32_t>(AllIndices.size());
                for (auto idx : local_indices)
                {
                    AllIndices.push_back(idx);
                }

                // Create cluster
                geom::cluster_data  cd;
                cd.m_PosScaleAndBaseBoneIndex.m_X   = pos_scale.m_X;
                cd.m_PosScaleAndBaseBoneIndex.m_Y   = pos_scale.m_Y;
                cd.m_PosScaleAndBaseBoneIndex.m_Z   = pos_scale.m_Z;
                cd.m_PosScaleAndBaseBoneIndex.m_W   = static_cast<float>(BaseBoneIndex);
                cd.m_PosTrasnlationAndWPADDING.m_X  = pos_center.m_X;
                cd.m_PosTrasnlationAndWPADDING.m_Y  = pos_center.m_Y;
                cd.m_PosTrasnlationAndWPADDING.m_Z  = pos_center.m_Z;
                cd.m_UVScaleTranslation.m_X         = uv_scale.m_X;
                cd.m_UVScaleTranslation.m_Y         = uv_scale.m_Y;
                cd.m_UVScaleTranslation.m_Z         = uv_min.m_X;
                cd.m_UVScaleTranslation.m_W         = uv_min.m_Y;
                AllClusterData.push_back(cd);

                geom::cluster       cl;
                cl.m_BBox                           = bb_pos.to_fbbox();
                cl.m_iIndex                         = cluster_index_start;
                cl.m_nIndices                       = static_cast<uint32_t>(c.tri_ids.size() * 3);
                cl.m_iVertex                        = cluster_vert_start;
                cl.m_nVertices                      = static_cast<uint32_t>(new_vert_ids.size());
                OutputClusters.push_back(cl);
            }
            else if (!bone_ok)
            {
                // Bone span is the correctness-blocking constraint (a hard 7-bit-per-vertex budget,
                // unlike position/UV extent which is just a quality target) - fix it first, ahead of
                // the position/UV axis competition below. Route each triangle by whether its
                // dominant (highest-weight, i.e. slot 0 - CleanWeights sorts descending) bone sits
                // above or below the span's midpoint; this provably shrinks the span each split.
                if (c.tri_ids.size() == 1)
                {
                    // A single triangle can't be split any further - if its own 3 vertices jointly
                    // still exceed the budget, no cluster-splitting strategy can fix it. (Each
                    // individual vertex was already checked in ConvertToCompilerMesh; this catches
                    // the case where two of a triangle's DIFFERENT vertices, each fine on their own,
                    // jointly span too far apart.)
                    throw std::runtime_error("A triangle's vertices jointly span more skeleton bones than a single cluster can address - author-time skeleton bone order makes it unrepresentable");
                }

                const std::int32_t mid_bone = (bb_bone.m_Min + bb_bone.m_Max) / 2;

                TriCluster c1, c2;
                for (uint32_t ti : c.tri_ids)
                {
                    uint32_t i0 = InputIndices[ti * 3 + 0];
                    uint32_t i1 = InputIndices[ti * 3 + 1];
                    uint32_t i2 = InputIndices[ti * 3 + 2];

                    const float avg_dominant_bone = (InputVerts[i0].m_SkelBoneIndex[0] + InputVerts[i1].m_SkelBoneIndex[0] + InputVerts[i2].m_SkelBoneIndex[0]) / 3.0f;

                    if (avg_dominant_bone < mid_bone) c1.tri_ids.push_back(ti);
                    else                              c2.tri_ids.push_back(ti);
                }

                // Degenerate route (every triangle's dominant bone landed on the same side) - fall
                // back to an arbitrary half/half split by triangle index so we still make progress.
                if (c1.tri_ids.empty() || c2.tri_ids.empty())
                {
                    c1.tri_ids.clear(); c2.tri_ids.clear();
                    for (std::size_t i = 0; i < c.tri_ids.size(); ++i)
                        (i < c.tri_ids.size() / 2 ? c1 : c2).tri_ids.push_back(c.tri_ids[i]);
                }

                RecurseClusterSplit(InputVerts, InputIndices, c1, MaxVerts, MaxExtent, BinormalSigns, OutputClusters, AllStaticVerts, AllExtrasVerts, AllClusterData, AllIndices);
                RecurseClusterSplit(InputVerts, InputIndices, c2, MaxVerts, MaxExtent, BinormalSigns, OutputClusters, AllStaticVerts, AllExtrasVerts, AllClusterData, AllIndices);
            }
            else
            {
                // Choose split axis based on max extent
                float   maxes[5]    = { extent_pos.m_X, extent_pos.m_Y, extent_pos.m_Z, extent_uv.m_X, extent_uv.m_Y };
                int     axis        = 0;
                float   max_val     = maxes[0];
                for (int i = 1; i < 5; ++i)
                {
                    if (maxes[i] > max_val)
                    {
                        max_val = maxes[i];
                        axis = i;
                    }
                }

                float   split_pos;
                bool    is_pos_axis = (axis < 3);
                int     sub_axis    = is_pos_axis ? axis : (axis - 3);
                if (is_pos_axis)
                {
                    split_pos = (bb_pos.m_MinPos[sub_axis] + bb_pos.m_MaxPos[sub_axis]) * 0.5f;
                }
                else
                {
                    split_pos = (bb_uv.m_MinUV[sub_axis] + bb_uv.m_MaxUV[sub_axis]) * 0.5f;
                }

                TriCluster c1, c2;
                for (uint32_t ti : c.tri_ids)
                {
                    uint32_t i0 = InputIndices[ti * 3 + 0];
                    uint32_t i1 = InputIndices[ti * 3 + 1];
                    uint32_t i2 = InputIndices[ti * 3 + 2];
                    float   cent;

                    if (is_pos_axis)
                    {
                        xmath::fvec3 cent_pos = (InputVerts[i0].m_Position + InputVerts[i1].m_Position + InputVerts[i2].m_Position) / 3.0f;
                        cent = cent_pos[sub_axis];
                    }
                    else
                    {
                        xmath::fvec2 cent_uv = (InputVerts[i0].m_UVs[0] + InputVerts[i1].m_UVs[0] + InputVerts[i2].m_UVs[0]) / 3.0f;
                        cent = cent_uv[sub_axis];
                    }

                    if (cent < split_pos)   c1.tri_ids.push_back(ti);
                    else                    c2.tri_ids.push_back(ti);
                }

                RecurseClusterSplit(InputVerts, InputIndices, c1, MaxVerts, MaxExtent, BinormalSigns, OutputClusters, AllStaticVerts, AllExtrasVerts, AllClusterData, AllIndices);
                RecurseClusterSplit(InputVerts, InputIndices, c2, MaxVerts, MaxExtent, BinormalSigns, OutputClusters, AllStaticVerts, AllExtrasVerts, AllClusterData, AllIndices);
            }
        }

        //--------------------------------------------------------------------------------------

        void ConvertToGeom(float target_precision)
        {
            const std::vector<mesh>&            compiler_meshes = m_CompilerMesh;
            geom&                               result          = m_FinalGeom;
            std::vector<geom::mesh>             OutMeshes;
            std::vector<geom::lod>              OutLODs;
            std::vector<geom::submesh>          OutSubmeshes;
            std::vector<geom::cluster>          OutClusters;
            std::vector<geom::vertex>           OutAllStaticVerts;
            std::vector<geom::vertex_extras>    OutAllExtrasVerts;
            std::vector<geom::cluster_data>     OutClusterData;
            std::vector<uint32_t>               OutAllIndices;
            BBox3                               OutGlobalBBox;
            std::uint16_t                       current_lod_idx         = 0;
            std::uint16_t                       current_submesh_idx     = 0;
            std::uint16_t                       current_cluster_idx     = 0;
            float                               max_extent              = target_precision * 65535.0f;

            for (const auto& input_mesh : compiler_meshes)
            {
                BBox3       mesh_bb         = {};
                float       total_edge_len  = 0.0f;
                uint32_t    num_edges       = 0;
                for (const auto& input_sm : input_mesh.m_SubMesh)
                {
                    for (const auto& v : input_sm.m_Vertex)
                    {
                        mesh_bb.Update(v.m_Position);
                        OutGlobalBBox.Update(v.m_Position);
                    }

                    for (size_t ti = 0; ti < input_sm.m_Indices.size() / 3; ++ti)
                    {
                        std::uint32_t i1 = input_sm.m_Indices[ti * 3 + 0];
                        std::uint32_t i2 = input_sm.m_Indices[ti * 3 + 1];
                        std::uint32_t i3 = input_sm.m_Indices[ti * 3 + 2];
                        total_edge_len  += (input_sm.m_Vertex[i1].m_Position - input_sm.m_Vertex[i2].m_Position).Length();
                        total_edge_len  += (input_sm.m_Vertex[i2].m_Position - input_sm.m_Vertex[i3].m_Position).Length();
                        total_edge_len  += (input_sm.m_Vertex[i3].m_Position - input_sm.m_Vertex[i1].m_Position).Length();
                        num_edges       += 3;
                    }

                    for (const auto& lod_in : input_sm.m_LODs)
                    {
                        for (size_t ti = 0; ti < lod_in.m_Indices.size() / 3; ++ti)
                        {
                            std::uint32_t i1 = lod_in.m_Indices[ti * 3 + 0];
                            std::uint32_t i2 = lod_in.m_Indices[ti * 3 + 1];
                            std::uint32_t i3 = lod_in.m_Indices[ti * 3 + 2];
                            total_edge_len  += (input_sm.m_Vertex[i1].m_Position - input_sm.m_Vertex[i2].m_Position).Length();
                            total_edge_len  += (input_sm.m_Vertex[i2].m_Position - input_sm.m_Vertex[i3].m_Position).Length();
                            total_edge_len  += (input_sm.m_Vertex[i3].m_Position - input_sm.m_Vertex[i1].m_Position).Length();
                            num_edges       += 3;
                        }
                    }
                }

                geom::mesh out_m;
                xstrtool::Copy(out_m.m_Name, input_mesh.m_Name);
                out_m.m_Name[31]        = '\0';
                out_m.m_WorldPixelSize  = (num_edges > 0) ? total_edge_len / num_edges : 0.0f;
                out_m.m_BBox            = mesh_bb.to_fbbox();
                out_m.m_nLODs           = static_cast<uint16_t>(input_mesh.m_SubMesh.empty() ? 1 : input_mesh.m_SubMesh[0].m_LODs.size() + 1);
                out_m.m_iLOD            = current_lod_idx;
                OutMeshes.push_back(out_m);

                current_lod_idx += out_m.m_nLODs;
                for (size_t lod_level = 0; lod_level < out_m.m_nLODs; ++lod_level)
                {
                    geom::lod out_l;
                    out_l.m_ScreenArea  = (lod_level == 0) ? 1.0f : (input_mesh.m_SubMesh.empty() ? 0.0f : input_mesh.m_SubMesh[0].m_LODs[lod_level - 1].m_ScreenArea);
                    out_l.m_iSubmesh    = current_submesh_idx;
                    out_l.m_nSubmesh    = static_cast<uint16_t>(input_mesh.m_SubMesh.size());
                    OutLODs.push_back(out_l);

                    current_submesh_idx += out_l.m_nSubmesh;
                    for (const auto& input_sm : input_mesh.m_SubMesh)
                    {
                        geom::submesh out_sm;
                        out_sm.m_iMaterial  = static_cast<uint16_t>(input_sm.m_iMaterial);
                        out_sm.m_iCluster   = current_cluster_idx;

                        const std::vector<uint32_t>&  lod_indices     = (lod_level == 0) ? input_sm.m_Indices : ((lod_level - 1 < input_sm.m_LODs.size()) ? input_sm.m_LODs[lod_level - 1].m_Indices : input_sm.m_Indices);
                        auto                          binormal_signs  = std::vector<float>(input_sm.m_Vertex.size(), 1.0f);
                        for (size_t i = 0; i < input_sm.m_Vertex.size(); ++i)
                        {
                            const vertex& v = input_sm.m_Vertex[i];
                            if (input_sm.m_bHasBTN)
                            {
                                xmath::fvec3    computed_binormal   = xmath::fvec3::Cross(v.m_Normal, v.m_Tangent);
                                float           dot_val             = xmath::fvec3::Dot(computed_binormal, v.m_Binormal);
                                binormal_signs[i] = (dot_val >= 0.0f) ? 1.0f : -1.0f;
                            }
                        }
                        TriCluster  initial  = {};
                        uint32_t    num_tris = static_cast<uint32_t>(lod_indices.size() / 3);

                        initial.tri_ids.resize(num_tris);
                        for (uint32_t i = 0; i < num_tris; ++i) initial.tri_ids[i] = i;

                        size_t prev_num_clusters = OutClusters.size();
                        RecurseClusterSplit(input_sm.m_Vertex, lod_indices, initial, 65534, max_extent, binormal_signs, OutClusters, OutAllStaticVerts, OutAllExtrasVerts, OutClusterData, OutAllIndices);

                        out_sm.m_nCluster    = static_cast<uint16_t>(OutClusters.size() - prev_num_clusters);
                        current_cluster_idx += out_sm.m_nCluster;
                        OutSubmeshes.push_back(out_sm);
                    }
                }
            }
            result.m_nMeshes    = static_cast<std::uint16_t>(OutMeshes.size());
            result.m_pMesh      = new geom::mesh[result.m_nMeshes];
            std::ranges::copy(OutMeshes, result.m_pMesh);
            result.m_nLODs      = static_cast<std::uint16_t>(OutLODs.size());
            result.m_pLOD       = new geom::lod[result.m_nLODs];
            std::ranges::copy(OutLODs, result.m_pLOD);
            result.m_nSubMeshs  = static_cast<std::uint16_t>(OutSubmeshes.size());
            result.m_pSubMesh   = new geom::submesh[result.m_nSubMeshs];
            std::ranges::copy(OutSubmeshes, result.m_pSubMesh);
            result.m_nClusters  = static_cast<std::uint16_t>(OutClusters.size());
            result.m_pCluster   = new geom::cluster[result.m_nClusters];
            std::ranges::copy(OutClusters, result.m_pCluster);
            result.m_BBox       = OutGlobalBBox.to_fbbox();
            result.m_nVertices  = static_cast<std::uint32_t>(OutAllStaticVerts.size());
            result.m_nIndices   = static_cast<std::uint32_t>(OutAllIndices.size());

            //
            // Set all the material instances
            //
            result.m_nDefaultMaterialInstances = static_cast<std::uint16_t>(m_RawGeom.m_MaterialInstance.size());
            result.m_pDefaultMaterialInstances = new xrsc::material_instance_ref[result.m_nDefaultMaterialInstances];

            // Set default values
            std::memset(result.m_pDefaultMaterialInstances, 0, result.m_nDefaultMaterialInstances * sizeof(*result.m_pDefaultMaterialInstances));

            // Set all the materials that we know about
            for (auto& E : m_RawGeom.m_MaterialInstance)
            {
                const auto Index = static_cast<int>(&E - m_RawGeom.m_MaterialInstance.data());
                if (int iMaterial = m_Descriptor.findMaterial(E.m_Name); iMaterial != -1)
                    result.m_pDefaultMaterialInstances[Index] = m_Descriptor.m_MaterialInstRefList[iMaterial];
            }

            //
            // Build the final data
            //
            // Compute aligned sizes for GPU data
            auto align = [](std::size_t offset, std::size_t alignment) constexpr -> std::size_t
            {
                return (offset + alignment - 1) & ~(alignment - 1);
            };

            constexpr std::size_t   vulkan_align    = 64; // Min for Vulkan buffers/UBO
            const std::size_t       VertexSize      = OutAllStaticVerts.size() * sizeof(geom::vertex);
            const std::size_t       ExtrasSize      = OutAllExtrasVerts.size() * sizeof(geom::vertex_extras);
            const std::size_t       IndicesSize     = OutAllIndices.size()     * sizeof(std::uint16_t);
            const std::size_t       ClusterDataSize = OutClusterData.size()    * sizeof(geom::cluster_data);

            std::size_t             current_offset  = 0;

            result.m_VertexOffset           = current_offset; current_offset = align(current_offset + VertexSize,       vulkan_align);
            result.m_VertexExtrasOffset     = current_offset; current_offset = align(current_offset + ExtrasSize,       vulkan_align);
            result.m_IndicesOffset          = current_offset; current_offset = align(current_offset + IndicesSize,      vulkan_align);
            result.m_ClusterDataOffset      = current_offset; current_offset = align(current_offset + ClusterDataSize,  vulkan_align);
            result.m_DataSize               = current_offset;
            result.m_pData                  = new char[result.m_DataSize];

            // Copy data into m_pData
            std::memcpy(result.m_pData + result.m_VertexOffset,         OutAllStaticVerts.data(), VertexSize);
            std::memcpy(result.m_pData + result.m_VertexExtrasOffset,   OutAllExtrasVerts.data(), ExtrasSize);
            std::memcpy(result.m_pData + result.m_ClusterDataOffset,    OutClusterData.data(),    ClusterDataSize);

            // Copy the indices
            auto pIndex = reinterpret_cast<std::uint16_t*>(result.m_pData + result.m_IndicesOffset);
            for (size_t i = 0; i < OutAllIndices.size(); ++i)
            {
                assert(OutAllIndices[i] < 0xffff);
                pIndex[i] = static_cast<std::uint16_t>(OutAllIndices[i]);
            }

            // Make sure that at least we have one cluster
            assert(result.m_nClusters >= 1);
        }

        //--------------------------------------------------------------------------------------

        static void MergeMeshes(std::unordered_map<std::string, xgeom_skin::mesh_details*>& OutHashMap, xraw3d::geom& Geom, const xgeom_skin::descriptor& Descriptor, const xgeom_skin::details& Details) noexcept
        {
            if (Descriptor.m_bMergeAllMeshes)
            {
                std::string newName = Descriptor.m_AllMeshesDetails.m_Name;
                Geom.CollapseMeshes(newName);
                if (!Geom.m_Mesh.empty())
                {
                    Geom.m_Mesh[0].m_Name = newName;
                }
                OutHashMap[newName] = const_cast<xgeom_skin::mesh_details*>(&Descriptor.m_AllMeshesDetails);
            }
            else
            {
                for (const auto& group : Descriptor.m_MergeGroupList)
                {
                    std::string newName = group.m_MeshDetails.m_Name;

                    std::unordered_set<int> olds;
                    for (const auto& np : group.m_NodePathList)
                    {
                        std::string prefix = np + "/";
                        for (int i = 0; i < static_cast<int>(Geom.m_Mesh.size()); ++i)
                        {
                            if (Geom.m_Mesh[i].m_ScenePath.starts_with(prefix))
                                olds.insert(i);
                        }
                    }

                    if (olds.empty()) continue;

                    int survivor = *std::min_element(olds.begin(), olds.end());

                    int maxBones = 0;
                    for (int i : olds)
                        maxBones = std::max(maxBones, Geom.m_Mesh[i].m_nBones);
                    Geom.m_Mesh[survivor].m_nBones = maxBones;

                    Geom.m_Mesh[survivor].m_Name = newName;

                    for (int i : olds)
                    {
                        if (i == survivor) continue;
                        for (auto& f : Geom.m_Facet)
                            if (f.m_iMesh == i)
                                f.m_iMesh = survivor;
                    }

                    // Delete non-survivors for this group
                    std::vector<int> groupDelete;
                    for (int i : olds)
                        if (i != survivor)
                            groupDelete.push_back(i);

                    std::sort(groupDelete.rbegin(), groupDelete.rend());

                    for (int i : groupDelete)
                        Geom.DeleteMesh(i);

                    OutHashMap[newName] = const_cast<xgeom_skin::mesh_details*>(&group.m_MeshDetails);
                }

                for (const auto& um : Descriptor.m_UngroupMeshList)
                {
                    std::string fullName = um.m_NodePath + "/" + um.m_MeshName;
                    int idx = Geom.findMeshByPath(fullName);
                    if (idx != -1)
                    {
                        std::string newName = um.m_MeshDetails.m_Name;
                        Geom.m_Mesh[idx].m_Name = newName;
                        OutHashMap[newName] = const_cast<xgeom_skin::mesh_details*>(&um.m_MeshDetails);
                    }
                }

                Geom.SortFacetsByMeshMaterialBone();
            }
        }

        //--------------------------------------------------------------------------------------

        void MergeMeshes()
        {
            //
            // Delete all the unwanted meshes
            //
            {
                std::vector<std::string> Path;
                std::function<void(const xgeom_skin::details::node&, bool)> Recursive = [&](const xgeom_skin::details::node& Node, bool bDelete )
                {
                    if (Path.empty()) Path.push_back(Node.m_Name);
                    else              Path.push_back( std::format("{}/{}", Path.back(), Node.m_Name ));

                    if ( bDelete == false )
                    {
                        if (m_Descriptor.isNodeInDeleteList(Path.back())) 
                            bDelete = true;
                    }

                    for (auto idx : Node.m_MeshList)
                    {
                        if (bDelete || m_Descriptor.isMeshInDeleteList(Path.back(), m_Details.m_MeshList[idx].m_Name))
                        {
                            std::string PathStr = std::format("{}/{}", Path.back(), m_Details.m_MeshList[idx].m_Name);
                            int         iRawMesh = m_RawGeom.findMeshByPath(PathStr);
                            if (iRawMesh != -1)
                            {
                                m_RawGeom.DeleteMesh(iRawMesh);
                            }
                        }
                    }

                    //
                    // Handle the children as well
                    //
                    for( auto& n : Node.m_Children)
                    {
                        Recursive(n, bDelete);
                    }

                    Path.pop_back();
                };

                Recursive(m_Details.m_RootNode, false);
            }

            //
            // Now we can actually merge the meshes
            //
            MergeMeshes(m_NameToDetail, m_RawGeom, m_Descriptor, m_Details);
        }

        //--------------------------------------------------------------------------------------

        xerr Compile()
        {
            try
            {
                //
                // First merge with the latest version of details
                //
                auto ErrorList = m_Descriptor.MergeWithDetails(m_Details);
                if (not ErrorList.empty())
                {
                    for (auto& e : ErrorList)
                        LogMessage( xresource_pipeline::msg_type::INFO, std::move(e) );
                }

                //
                // Merge meshes
                //
                {
                    displayProgressBar("Merging Meshes", 1);
                    MergeMeshes();
                    displayProgressBar("Merging Meshes", 0);
                }

                //
                // Apply the referenced SKELETON's own author-time PreTransform to every vertex -
                // mirrors xanim_package_compiler's BuildCompiledClip applying this same correction to
                // an animated root bone's curve, for the identical reason: this mesh's raw import
                // (m_RawGeom, from OUR OWN independent assimp import of the same file) never passed
                // through the skeleton's import/PreTransform step, so without this the mesh sits in
                // raw-FBX space while the skeleton's compiled bind poses (m_InvBindPose/m_RestPose,
                // both already corrected - see xskeleton_compiler's ApplyPreTransform) sit in engine
                // space. Skinning a mesh against bind poses in a DIFFERENT space than the mesh's own
                // is exactly what produces the "hands fly off" style distortion this fixes: the error
                // compounds down the kinematic chain, so it's most visible on the most distal bones
                // (fingers/hands) even though every bone is technically affected. Mandatory (not
                // gated on being non-identity like the user's own PreTranslation below) since it's a
                // correctness requirement, not an authoring convenience - though the check still
                // avoids a no-op matrix multiply over every vertex when the skeleton has none.
                //
                if (   m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformScale       != xmath::fvec3::fromOne()
                    || m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformTranslation != xmath::fvec3::fromZero()
                    || m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformRotationDeg != xmath::fvec3::fromZero() )
                {
                    displayProgressBar("ApplyingSkeletonPreTransform", 0);

                    xmath::radian3 Rot;
                    Rot.m_Roll  = xmath::radian{ xmath::DegToRad(m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformRotationDeg.m_Z) };
                    Rot.m_Pitch = xmath::radian{ xmath::DegToRad(m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformRotationDeg.m_X) };
                    Rot.m_Yaw   = xmath::radian{ xmath::DegToRad(m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformRotationDeg.m_Y) };

                    const xmath::fmat4 M(m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformScale, Rot, m_Descriptor.m_ResolvedSkeletonBones.m_PreTransformTranslation);
                    const xmath::fmat3 M2(Rot);
                    for (auto& E : m_RawGeom.m_Vertex)
                    {
                        E.m_Position = M * E.m_Position;

                        E.m_BTN[0].m_Normal   = M2 * E.m_BTN[0].m_Normal;
                        E.m_BTN[0].m_Binormal = M2 * E.m_BTN[0].m_Binormal;
                        E.m_BTN[0].m_Tangent  = M2 * E.m_BTN[0].m_Tangent;
                    }

                    displayProgressBar("ApplyingSkeletonPreTransform", 1);
                }

                //
                // Pre-Transform the verts (the descriptor's OWN, separate, author-controlled
                // adjustment - applied on top of the mandatory skeleton correction above).
                //
                if (   m_Descriptor.m_PreTranslation.m_Scale       != xmath::fvec3::fromOne()
                    || m_Descriptor.m_PreTranslation.m_Translation != xmath::fvec3::fromZero()
                    || m_Descriptor.m_PreTranslation.m_Rotation    != xmath::fvec3::fromZero() )
                {
                    displayProgressBar("PreTranslatingMeshes", 0);

                    xmath::radian3 Rot;
                    Rot.m_Roll  = xmath::radian{ xmath::DegToRad(m_Descriptor.m_PreTranslation.m_Rotation.m_Z) };
                    Rot.m_Pitch = xmath::radian{ xmath::DegToRad(m_Descriptor.m_PreTranslation.m_Rotation.m_X) };
                    Rot.m_Yaw   = xmath::radian{ xmath::DegToRad(m_Descriptor.m_PreTranslation.m_Rotation.m_Y) };

                    const xmath::fmat4 M(m_Descriptor.m_PreTranslation.m_Scale, Rot, m_Descriptor.m_PreTranslation.m_Translation);
                    const xmath::fmat3 M2(Rot);
                    for (auto& E : m_RawGeom.m_Vertex )
                    {
                        E.m_Position = M * E.m_Position;

                        E.m_BTN[0].m_Normal   = M2 * E.m_BTN[0].m_Normal;
                        E.m_BTN[0].m_Binormal = M2 * E.m_BTN[0].m_Binormal;
                        E.m_BTN[0].m_Tangent  = M2 * E.m_BTN[0].m_Tangent;
                    }

                    displayProgressBar("PreTranslatingMeshes", 1);
                }

                //
                // Normalize Mesh
                //
                {
                    // Force every vert to have a single UV, normals, etc...
                    for (auto& V : m_RawGeom.m_Vertex)
                    {
                        V.m_nUVs        = 1;
                        assert(V.m_nNormals    == 1);
                        assert(V.m_nTangents   == 1);
                        assert(V.m_nBinormals  == 1);
                        V.m_nColors     = 0;
                    }
                }

                //
                // Clean the mesh
                //
                {
                    displayProgressBar("Cleaning up Geom", 1);
                    m_RawGeom.ComputeTangentsAndBinormalsMikk();
                    m_RawGeom.CleanMesh();
                    m_RawGeom.SortFacetsByMeshMaterialBone();
                    displayProgressBar("Cleaning up Geom", 1);
                }

                //
                // Sort/cap/normalize bone weights to our 4-influence budget BEFORE resolving bone
                // names in ConvertToCompilerMesh - only weights that survive this cull ever need to
                // resolve to a skeleton bone at all.
                //
                {
                    constexpr float min_weight_v = 1.0f / 512.0f;
                    m_RawGeom.CleanWeights(4, min_weight_v);
                }

                //
                // Convert to mesh
                //
                m_FinalGeom.Initialize();

                displayProgressBar("Generating LODs", 1);
                if (auto Err = ConvertToCompilerMesh(); Err)
                    return Err;
                GenenateLODs();
                displayProgressBar("Generating LODs", 0);

                //
                // Generate final mesh
                //
                displayProgressBar("Generating Final Mesh", 0);

                // mm accuracy
                ConvertToGeom(0.001f);
                
                displayProgressBar("Generating Final Mesh", 1);
            }
            catch (std::runtime_error Error )
            {
                LogMessage(xresource_pipeline::msg_type::ERROR, std::format("{}", Error.what()));
                return xerr::create_f<state, "Exception thrown">();
            }

            return {};
        }

        //--------------------------------------------------------------------------------------

        void ComputeDetailStructure()
        {
            // Collect all the meshes
            m_Details.m_MeshList.resize(m_RawGeom.m_Mesh.size());
            for ( auto& M : m_RawGeom.m_Mesh )
            {
                const auto  index   = static_cast<int>(&M - m_RawGeom.m_Mesh.data());
                auto&       DM      = m_Details.m_MeshList[index];

                DM.m_Name           = M.m_Name;
                DM.m_NumFaces       = 0;
                DM.m_NumUVs         = 0;
                DM.m_NumColors      = 0;
                DM.m_NumNormals     = 0;
                DM.m_NumTangents    = 0;
            }

            std::vector<std::vector<int>> MeshMatUsage;

            // prepare to count how many material per mesh we use
            MeshMatUsage.resize(m_RawGeom.m_Mesh.size());
            for (auto& M : m_RawGeom.m_Mesh)
            {
                const auto  index = static_cast<int>(&M - m_RawGeom.m_Mesh.data());
                MeshMatUsage[index].resize(m_RawGeom.m_MaterialInstance.size());
                for (auto& I : MeshMatUsage[index]) I = 0;
            }

            // Add all the faces
            for( auto& F : m_RawGeom.m_Facet)
            {
                const auto  index = static_cast<int>(&F - m_RawGeom.m_Facet.data());
                m_Details.m_MeshList[F.m_iMesh].m_NumFaces++;

                for ( int i=0; i<F.m_nVertices; ++i)
                {
                    m_Details.m_MeshList[F.m_iMesh].m_NumColors   = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumColors,   m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nColors );
                    m_Details.m_MeshList[F.m_iMesh].m_NumUVs      = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumUVs,      m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nUVs    );
                    m_Details.m_MeshList[F.m_iMesh].m_NumNormals  = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumNormals,  m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nNormals);
                    m_Details.m_MeshList[F.m_iMesh].m_NumTangents = std::max(m_Details.m_MeshList[F.m_iMesh].m_NumTangents, m_RawGeom.m_Vertex[F.m_iVertex[0]].m_nTangents);
                }

                MeshMatUsage[F.m_iMesh][F.m_iMaterialInstance]++;
            }

            // Set the material instance counts...
            for ( auto& M : m_Details.m_MeshList)
            {
                const auto  index = static_cast<int>(&M - m_Details.m_MeshList.data());

                // Set all the materials used by this mesh
                for (auto& E : MeshMatUsage[index])
                {
                    if ( E == 0 ) continue;

                    const auto  iMat = static_cast<int>(&E - MeshMatUsage[index].data());
                    M.m_MaterialList.push_back(iMat);
                }
            }

            // collect all the nodes
            std::function<void(xgeom_skin::details::node&, const xraw3d::assimp_v3::node&)> CollectNodes = [&](xgeom_skin::details::node& FinalNode, const xraw3d::assimp_v3::node& Node )
            {
                FinalNode.m_Name     = Node.m_Name;
                FinalNode.m_MeshList.reserve(Node.m_MeshList.size());

                for ( auto& E : Node.m_MeshList ) 
                {
                    assert( E < m_Details.m_MeshList.size() );
                    FinalNode.m_MeshList.emplace_back(static_cast<int>(E));
                }

                for ( auto& E : Node.m_Children ) CollectNodes( FinalNode.m_Children.emplace_back(), E);
            };
            CollectNodes(m_Details.m_RootNode, m_RootNode);

            // Set all the materials
            for (auto& E : m_RawGeom.m_MaterialInstance)
            {
                m_Details.m_MaterialList.emplace_back(E.m_Name);
            }

            // Find total faces
            m_Details.m_NumFaces        = 0;
            for (auto& M : m_Details.m_MeshList)
            {
                m_Details.m_NumFaces += M.m_NumFaces;
            }
        }

        //--------------------------------------------------------------------------------------

        xerr onCompile(void) noexcept override
        {
            //
            // Read the descriptor file...
            //
            displayProgressBar("Loading Descriptor", 0);
            {
                xproperty::settings::context    Context{};
                auto                            DescriptorFileName = std::format(L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);

                if ( auto Err = m_Descriptor.Serialize(true, DescriptorFileName, Context); Err)
                    return Err;
            }

            //
            // Do a quick validation of the descriptor
            //
            {
                std::vector<std::string> Errors;
                m_Descriptor.Validate(Errors);
                if (not Errors.empty())
                {
                    for( auto& E : Errors)
                        LogMessage(xresource_pipeline::msg_type::ERROR, std::move(E) );

                    return xerr::create_f<state, "Validation Errors">();
                }
            }
            displayProgressBar("Loading Descriptor", 1);

            //
            // Build the raw-bone-name -> skeleton-bone-index lookup used by ConvertToCompilerMesh.
            // The descriptor's own Serialize() override above already read the referenced skeleton's
            // bone manifest (RunAfter in Plugin.config guarantees it exists by now).
            //
            BuildBoneNameMap();

            //
            // Load the source data
            //
            displayProgressBar("Loading Mesh", 0);
            if ( auto Err = LoadRaw(std::format(L"{}/{}", m_ProjectPaths.m_Project, m_Descriptor.m_ImportAsset)); Err )
                return Err;
            displayProgressBar("Loading Mesh", 1);

            //
            // Fill the detail structure
            //
            ComputeDetailStructure();

            //
            // OK Time to compile
            //
            Compile();

            //
            // Serialize the details structure
            //
            {
                xtextfile::stream File;
                if (auto Err = File.Open(false, std::format(L"{}\\Details.txt", m_ResourceLogPath), xtextfile::file_type::TEXT); Err)
                    return xerr::create_f<state, "Failed while opening the details.txt so it can't be saved">(Err);

                xproperty::settings::context C{};
                if ( auto Err = xproperty::sprop::serializer::Stream( File, m_Details, C); Err )
                    return xerr::create_f<state, "Failed while serializing details.txt">(Err);
            }

            //
            // Export
            //
            int Count = 0;
            for (auto& T : m_Target)
            {
                displayProgressBar("Serializing", Count++ / (float)m_Target.size());

                if (T.m_bValid)
                {
                    Serialize(T.m_DataPath);
                }
            }
            displayProgressBar("Serializing", 1);
            return {};
        }

        //--------------------------------------------------------------------------------------

        void Serialize(const std::wstring_view FilePath)
        {
            xserializer::stream Serializer;
            if( auto Err = Serializer.Save
                ( FilePath
                , m_FinalGeom
                , m_OptimizationType == optimization_type::O0 ? xserializer::compression_level::FAST : m_OptimizationType == optimization_type::O1 ? xserializer::compression_level::MEDIUM : xserializer::compression_level::HIGH
                ); Err )
            {
                throw(std::runtime_error(std::string(Err.getMessage())));
            }
        }

        meshopt_VertexCacheStatistics   m_VertCacheAMDStats;
        meshopt_VertexCacheStatistics   m_VertCacheNVidiaStats;
        meshopt_VertexCacheStatistics   m_VertCacheIntelStats;
        meshopt_VertexCacheStatistics   m_VertCacheStats;
        meshopt_VertexFetchStatistics   m_VertFetchStats;
        meshopt_OverdrawStatistics      m_OverdrawStats;
        std::unordered_map<std::string, xgeom_skin::mesh_details*> m_NameToDetail;
        xgeom_skin::details           m_Details;
        xgeom_skin::descriptor        m_Descriptor;

        xgeom_skin::geom              m_FinalGeom;
        std::vector<mesh>               m_CompilerMesh;
        xraw3d::geom                    m_RawGeom;
        xraw3d::assimp_v3::node         m_RootNode;
        std::unordered_map<std::string, int> m_BoneNameToSkelIndex;   // built once by BuildBoneNameMap()
    };

    //------------------------------------------------------------------------------------

    std::unique_ptr<instance> instance::Create(void)
    {
        return std::make_unique<implementation>();
    }
}


