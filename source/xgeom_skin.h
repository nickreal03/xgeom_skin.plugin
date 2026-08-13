#ifndef XGEOM_SKIN_RUNTIME_H
#define XGEOM_SKIN_RUNTIME_H
#pragma once

#include "dependencies/xmath/source/xmath_fshapes.h"
#include "dependencies/xserializer/source/xserializer.h"
#include <span>  // Add for std::span
#include <algorithm>

namespace xgeom_skin
{
    struct geom
    {
        inline static constexpr auto xserializer_version_v = 1;
        struct mesh
        {
            std::array<char, 32>    m_Name;
            float                   m_WorldPixelSize;   // Average World Pixel size for this SubMesh
            xmath::fbbox            m_BBox;
            std::uint16_t           m_nLODs;
            std::uint16_t           m_iLOD;
        };

        struct lod
        {
            float                   m_ScreenArea;
            std::uint16_t           m_iSubmesh;         // Start the submeshes
            std::uint16_t           m_nSubmesh;
        };

        struct submesh
        {
            std::uint16_t           m_iCluster;         // Where the index starts
            std::uint16_t           m_nCluster;         // Where the index starts
            std::uint16_t           m_iMaterial;        // Index of the Material that this SubMesh uses
        };

        struct vec3
        {
            float m_X, m_Y, m_Z;
        };

        struct vec2
        {
            float m_X, m_Y;
        };

        struct vec4
        {
            float m_X, m_Y, m_Z, m_W;
        };

        // Same GPU-side layout as xgeom_static's cluster_data, except the W component of the
        // position-scale vec4 - "trash"/unused padding in xgeom_static - now carries this cluster's
        // base skeleton bone index. A vertex's real bone index for influence i is
        // m_BaseBoneIndex + vertex::getBoneOffset(i). Stored as a plain float: bone counts never get
        // anywhere near 2^24, so the round trip through float is exact, and it costs zero extra bytes.
        struct cluster_data
        {
            vec4                    m_PosScaleAndBaseBoneIndex;   // XYZ = position scale for the cluster, W = base skeleton bone index
            vec4                    m_PosTrasnlationAndWPADDING;  // XYZ = position translation for the cluster, W = unused (mirrors xgeom_static)
            vec4                    m_UVScaleTranslation;         // UV Translation.
        };

        struct cluster
        {
            xmath::fbbox            m_BBox;                     // Optional fine-grained CPU culling (e.g., per-cluster frustum/occlusion)
            std::uint32_t           m_iIndex;                   // Where the index starts
            std::uint32_t           m_nIndices;                 // number of
            std::uint32_t           m_iVertex;                  // Where the vertex starts
            std::uint32_t           m_nVertices;                // number of
        };

        // Position fused with skin data - a skinned vertex's position is meaningless without its
        // bone weights, so (unlike UV/normal/tangent, which a shadow-only pass can legitimately skip)
        // they belong in the same stream, both for correctness of intent and for cache locality during
        // deform. 12 bytes, zero padding: m_Packed's 48 bits are hand bit-packed rather than using
        // byte-per-field storage specifically so nothing is wasted (see getBoneOffset/getWeights below).
        struct vertex
        {
            int16_t                     m_XPos, m_YPos, m_ZPos;   // unchanged from xgeom_static, full int16 precision
            std::array<std::uint8_t,6>  m_Packed;                 // see bit layout below

            // m_Packed's 48 bits, LSB-first (bit 0 = lowest bit of m_Packed[0]):
            //   bits  0- 6  BoneOffset[0]  (7 bits, cluster-local: real index = cluster.m_BaseBoneIndex + this)
            //   bits  7-13  BoneOffset[1]  (7 bits)
            //   bits 14-20  BoneOffset[2]  (7 bits)
            //   bits 21-27  BoneOffset[3]  (7 bits)
            //   bits 28-35  Weight0        (8 bits, true range [0.25,1.0] - largest of <=4 weights summing to 1)
            //   bits 36-42  Weight1        (7 bits, true range [0,0.5])
            //   bits 43-47  Weight2        (5 bits, true range [0,0.333..])
            // Weight3 is never stored - it's always exactly 1-(Weight0+Weight1+Weight2). This is also
            // the whole weight-truncation LOD scheme: at MaxInfluences=k, read k-1 stored weights and
            // force the k-th used weight to 1-sum(the rest).
            static constexpr int    bone_offset_bits_v  = 7;
            static constexpr int    weight0_bits_v      = 8;
            static constexpr int    weight1_bits_v      = 7;
            static constexpr int    weight2_bits_v      = 5;
            static constexpr int    max_bone_offset_v   = (1 << bone_offset_bits_v) - 1;   // 127
            static constexpr float  weight0_low_v       = 0.25f;
            static constexpr float  weight0_range_v     = 1.0f - weight0_low_v;            // [0.25,1.0]
            static constexpr float  weight1_range_v     = 0.5f;                            // [0,0.5]
            static constexpr float  weight2_range_v     = 1.0f / 3.0f;                     // [0,0.333..]

            inline std::uint64_t getRawPacked(void) const noexcept
            {
                std::uint64_t V = 0;
                for (int i = 5; i >= 0; --i) V = (V << 8) | m_Packed[i];
                return V;
            }

            inline void setRawPacked(std::uint64_t V) noexcept
            {
                for (int i = 0; i < 6; ++i) { m_Packed[i] = static_cast<std::uint8_t>(V & 0xFF); V >>= 8; }
            }

            inline std::uint8_t getBoneOffset(int i) const noexcept
            {
                return static_cast<std::uint8_t>((getRawPacked() >> (i * bone_offset_bits_v)) & max_bone_offset_v);
            }

            // Returns all 4 weights, sorted descending, summing to 1 (Weight[3] is the derived one)
            inline std::array<float,4> getWeights(void) const noexcept
            {
                const std::uint64_t V  = getRawPacked();
                const std::uint32_t Q0 = static_cast<std::uint32_t>((V >> 28) & ((1u << weight0_bits_v) - 1));
                const std::uint32_t Q1 = static_cast<std::uint32_t>((V >> 36) & ((1u << weight1_bits_v) - 1));
                const std::uint32_t Q2 = static_cast<std::uint32_t>((V >> 43) & ((1u << weight2_bits_v) - 1));

                const float W0 = weight0_low_v + (Q0 / float((1 << weight0_bits_v) - 1)) * weight0_range_v;
                const float W1 =                 (Q1 / float((1 << weight1_bits_v) - 1)) * weight1_range_v;
                const float W2 =                 (Q2 / float((1 << weight2_bits_v) - 1)) * weight2_range_v;
                const float W3 = std::max(0.0f, 1.0f - (W0 + W1 + W2));
                return { W0, W1, W2, W3 };
            }

            // BoneOffset/Weight must already be sorted descending by weight and sum to 1
            // (xraw3d::geom::CleanWeights guarantees this upstream in the compiler).
            static inline vertex Pack(std::int16_t X, std::int16_t Y, std::int16_t Z, std::array<std::uint8_t,4> BoneOffset, std::array<float,4> Weight) noexcept
            {
                vertex V{};
                V.m_XPos = X; V.m_YPos = Y; V.m_ZPos = Z;

                const std::uint32_t Q0 = static_cast<std::uint32_t>(std::clamp((Weight[0] - weight0_low_v) / weight0_range_v, 0.0f, 1.0f) * ((1 << weight0_bits_v) - 1) + 0.5f);
                const std::uint32_t Q1 = static_cast<std::uint32_t>(std::clamp( Weight[1] / weight1_range_v,                 0.0f, 1.0f) * ((1 << weight1_bits_v) - 1) + 0.5f);
                const std::uint32_t Q2 = static_cast<std::uint32_t>(std::clamp( Weight[2] / weight2_range_v,                 0.0f, 1.0f) * ((1 << weight2_bits_v) - 1) + 0.5f);

                std::uint64_t Raw = 0;
                for (int i = 0; i < 4; ++i) Raw |= (std::uint64_t(BoneOffset[i]) & max_bone_offset_v) << (i * bone_offset_bits_v);
                Raw |= std::uint64_t(Q0) << 28;
                Raw |= std::uint64_t(Q1) << 36;
                Raw |= std::uint64_t(Q2) << 43;

                V.setRawPacked(Raw);
                return V;
            }
        };
        static_assert(sizeof(vertex) == 12);

        // Absorbs xgeom_static's m_Extra (oct normal/tangent low bits + binormal sign) at upgraded
        // precision, now that alignment isn't forcing a 12/11-bit split on this data - it's shading
        // data, not deform data, so it has no reason to live alongside position/skin.
        struct vertex_extras
        {
            std::array<std::uint16_t,2>  m_UV;
            std::array<std::uint16_t,2>  m_OctNormal;
            std::uint16_t                m_OctTangentX;
            std::uint16_t                m_OctTangentY_Sign;   // bits 0-14 = tangent Y (15 bits), bit 15 = binormal sign (0:+1, 1:-1)
        };
        static_assert(sizeof(vertex_extras) == 12);

        using runtime_allocation = std::array<std::size_t, 4*(sizeof(std::shared_ptr<int>) / sizeof(std::size_t))>;

        //-------------------------------------------------------------------------

                                                        geom                        (void)                                      noexcept = default;
        inline                                          geom                        (xserializer::stream& Steaming)             noexcept;
        inline void                                     Kill                        (void)                                      noexcept;
        inline void                                     Initialize                  (void)                                      noexcept;
        inline int                                      findMeshIndex               (const char* pName)                 const   noexcept;
        inline int                                      getSubMeshIndex             (int iMesh, int iMaterialInstance)  const   noexcept;
        inline std::span<mesh>                          getMeshes                   (void)                              const   noexcept { return { m_pMesh, m_nMeshes }; }
        inline std::span<lod>                           getLODs                     (void)                              const   noexcept { return { m_pLOD, m_nLODs }; }
        inline std::span<submesh>                       getSubmeshes                (void)                              const   noexcept { return { m_pSubMesh, m_nSubMeshs }; }
        inline std::span<cluster>                       getClusters                 (void)                              const   noexcept { return { m_pCluster, m_nClusters }; }
        inline std::span<vertex>                        getVertices                 (void)                              const   noexcept { return { reinterpret_cast<vertex*>       (m_pData + m_VertexOffset),         m_nVertices }; }
        inline std::span<vertex_extras>                 getVertexExtras             (void)                              const   noexcept { return { reinterpret_cast<vertex_extras*>(m_pData + m_VertexExtrasOffset),   m_nVertices }; }
        inline std::span<std::uint16_t>                 getIndices                  (void)                              const   noexcept { return { reinterpret_cast<std::uint16_t*>(m_pData + m_IndicesOffset),        m_nIndices  }; }
        inline std::span<cluster_data>                  getClusterData              (void)                              const   noexcept { return { reinterpret_cast<cluster_data*> (m_pData + m_ClusterDataOffset),    m_nClusters }; }
        inline std::span<xrsc::material_instance_ref>   getDefaultMaterialInstances (void)                              const   noexcept { return { m_pDefaultMaterialInstances, m_nDefaultMaterialInstances }; }

        xmath::fbbox                    m_BBox;
        char*                           m_pData;  // Contiguous buffer for GPU data ( vertices, extras, indices)
        mesh*                           m_pMesh;  // Separate allocations for CPU-persistent data
        lod*                            m_pLOD;
        submesh*                        m_pSubMesh;
        cluster*                        m_pCluster;
        xrsc::material_instance_ref*    m_pDefaultMaterialInstances;
        runtime_allocation              m_RunTimeSpace;
        std::size_t                     m_DataSize;
        std::size_t                     m_VertexOffset;
        std::size_t                     m_VertexExtrasOffset;
        std::size_t                     m_IndicesOffset;
        std::size_t                     m_ClusterDataOffset;
        std::uint16_t                   m_nMeshes;
        std::uint16_t                   m_nLODs;
        std::uint16_t                   m_nSubMeshs;
        std::uint16_t                   m_nClusters;
        std::uint32_t                   m_nIndices;
        std::uint32_t                   m_nVertices;
        std::uint16_t                   m_nDefaultMaterialInstances;
    };

    //-------------------------------------------------------------------------

    geom::geom(xserializer::stream& Steaming) noexcept
    {
        //xassert( Steaming.getResourceVersion() == xgeom::VERSION );
    }

    //-------------------------------------------------------------------------

    void geom::Initialize(void) noexcept
    {
        std::memset(this, 0, sizeof(*this));
    }

    //-------------------------------------------------------------------------
    void geom::Kill(void) noexcept
    {
        if (m_pMesh)                        delete[] m_pMesh;
        if (m_pLOD)                         delete[] m_pLOD;
        if (m_pSubMesh)                     delete[] m_pSubMesh;
        if (m_pCluster)                     delete[] m_pCluster;
        if (m_pDefaultMaterialInstances)    delete[] m_pDefaultMaterialInstances;
        if (m_pData)                        delete[] m_pData;

        Initialize();
    }

    //-------------------------------------------------------------------------

    int geom::findMeshIndex(const char* pName) const noexcept
    {
        auto meshes = getMeshes();
        for (auto i = 0u; i < meshes.size(); i++)
        {
            if (!std::strcmp(meshes[i].m_Name.data(), pName))
            {
                return i;
            }
        }
        return -1;
    }
}

//-------------------------------------------------------------------------
// serializer
//-------------------------------------------------------------------------
namespace xserializer::io_functions
{
    //-------------------------------------------------------------------------
    template<> inline
        xerr SerializeIO<xgeom_skin::geom::lod>(xserializer::stream& Stream, const xgeom_skin::geom::lod& Lod) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Lod.m_ScreenArea))
            || (Err = Stream.Serialize(Lod.m_iSubmesh))
            || (Err = Stream.Serialize(Lod.m_nSubmesh))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_skin::geom::mesh>(xserializer::stream& Stream, const xgeom_skin::geom::mesh& Mesh) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Mesh.m_Name))
            || (Err = Stream.Serialize(Mesh.m_WorldPixelSize))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Min.m_X))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Min.m_Y))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Min.m_Z))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Max.m_X))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Max.m_Y))
            || (Err = Stream.Serialize(Mesh.m_BBox.m_Max.m_Z))
            || (Err = Stream.Serialize(Mesh.m_nLODs))
            || (Err = Stream.Serialize(Mesh.m_iLOD))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_skin::geom::submesh>(xserializer::stream& Stream, const xgeom_skin::geom::submesh& Submesh) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Submesh.m_iCluster))
            || (Err = Stream.Serialize(Submesh.m_nCluster))
            || (Err = Stream.Serialize(Submesh.m_iMaterial))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_skin::geom::cluster>(xserializer::stream& Stream, const xgeom_skin::geom::cluster& Cluster) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Cluster.m_nVertices))
            || (Err = Stream.Serialize(Cluster.m_nIndices))
            || (Err = Stream.Serialize(Cluster.m_iIndex))
            || (Err = Stream.Serialize(Cluster.m_iVertex))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Min.m_X))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Min.m_Y))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Min.m_Z))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Max.m_X))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Max.m_Y))
            || (Err = Stream.Serialize(Cluster.m_BBox.m_Max.m_Z))
            ;
        return Err;
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xrsc::material_instance_ref>(xserializer::stream& Stream, const xrsc::material_instance_ref& IR) noexcept
    {
        return Stream.Serialize(IR.m_Instance.m_Value);
    }

    //-------------------------------------------------------------------------
    template<> inline
    xerr SerializeIO<xgeom_skin::geom>(xserializer::stream& Stream, const xgeom_skin::geom& Geom) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(Geom.m_nMeshes))
            || (Err = Stream.Serialize(Geom.m_pMesh,                        Geom.m_nMeshes))
            || (Err = Stream.Serialize(Geom.m_nLODs))
            || (Err = Stream.Serialize(Geom.m_pLOD,                         Geom.m_nLODs))
            || (Err = Stream.Serialize(Geom.m_nSubMeshs))
            || (Err = Stream.Serialize(Geom.m_pSubMesh,                     Geom.m_nSubMeshs))
            || (Err = Stream.Serialize(Geom.m_nClusters))
            || (Err = Stream.Serialize(Geom.m_pCluster,                     Geom.m_nClusters))
            || (Err = Stream.Serialize(Geom.m_nDefaultMaterialInstances))
            || (Err = Stream.Serialize(Geom.m_pDefaultMaterialInstances,    Geom.m_nDefaultMaterialInstances))
            || (Err = Stream.Serialize(Geom.m_DataSize))
            || (Err = Stream.Serialize(Geom.m_pData,                        Geom.m_DataSize))
            || (Err = Stream.Serialize(Geom.m_RunTimeSpace))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Min.m_X))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Min.m_Y))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Min.m_Z))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Max.m_X))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Max.m_Y))
            || (Err = Stream.Serialize(Geom.m_BBox.m_Max.m_Z))
            || (Err = Stream.Serialize(Geom.m_VertexOffset))
            || (Err = Stream.Serialize(Geom.m_VertexExtrasOffset))
            || (Err = Stream.Serialize(Geom.m_IndicesOffset))
            || (Err = Stream.Serialize(Geom.m_ClusterDataOffset))
            || (Err = Stream.Serialize(Geom.m_nVertices))
            || (Err = Stream.Serialize(Geom.m_nIndices))
            ;
        return Err;
    }
}

#endif
