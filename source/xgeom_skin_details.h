#ifndef XGEOM_SKIN_DETAILS_H
#define XGEOM_SKIN_DETAILS_H
#pragma once

namespace xgeom_skin
{
    struct details
    {
        struct mesh
        {
            std::string                 m_Name          = {};
            std::vector<int>            m_MaterialList  = {};
            int                         m_NumFaces      = 0;
            int                         m_NumUVs        = 0;
            int                         m_NumColors     = 0;
            int                         m_NumNormals    = 0;
            int                         m_NumTangents   = 0;

            XPROPERTY_DEF
            ( "mesh", mesh
            , obj_member<"Name",            &mesh::m_Name >
            , obj_member<"NumMaterials",    &mesh::m_MaterialList >
            , obj_member<"NumFaces",        &mesh::m_NumFaces >
            , obj_member<"NumUVs",          &mesh::m_NumUVs >
            , obj_member<"NumColors",       &mesh::m_NumColors >
            )
        };

        struct node
        {
            std::string                 m_Name;
            std::vector<int>            m_MeshList;
            std::vector<node>           m_Children;

            XPROPERTY_DEF
            ( "node", node
            , obj_member<"Name",        &node::m_Name >
            , obj_member<"Mesh",        &node::m_MeshList >
            , obj_member<"Children",    &node::m_Children >
            )
        };

        using node_path = std::vector<std::string>;

        int findMaterial(std::string_view Name) const noexcept
        {
            for (auto& E : m_MaterialList)
                if (E == Name) return static_cast<int>(&E - m_MaterialList.data());
            return -1;
        }


        static bool CompareNodePaths(const node_path& A, const node_path& B)
        {
            if (A.size() != B.size()) return false;
            if (A.empty() && B.empty()) return false;

            for (int i = 0; i < A.size(); ++i)
            {
                if (A[i] != B[i]) return false;
            }

            return true;
        }

        /*
        std::pair<const node*, const node*> findNode( std::span<const std::string> NodePath, const node* pNode = nullptr, const node* pParent = nullptr ) const noexcept
        {
            if (pNode == nullptr) pNode = &m_RootNode;
            if (pNode->m_Name == NodePath[0] ) return {pNode, pParent};
            if( NodePath.size() == 1) return{};

            for( auto& n : pNode->m_Children )
            {
                auto Pair = findNode(std::span(NodePath).subspan(1), &n, pNode);
                if (Pair.first) return Pair;
            }

            return {};
        }
        */

        std::pair<const node*, const node*> findNode(std::span<const std::string> NodePath) const noexcept
        {
            if ( NodePath.empty() ) return {};
            if (NodePath.size() == 1 && NodePath[0] == m_RootNode.m_Name) return {&m_RootNode, nullptr};
            if (NodePath[0] != m_RootNode.m_Name) return {};

            int Index=1;
            const node* pParent = &m_RootNode;
            do
            {
                bool bFound = false;
                for (const node& Node : pParent->m_Children)
                {
                    if (Node.m_Name == NodePath[Index])
                    {
                        Index++;
                        if (Index == NodePath.size())
                            return{ &Node, pParent };

                        // Otherwise just keep searching
                        pParent = &Node;
                        bFound = true;
                        break;
                    }
                }

                if (bFound==false) break;

            } while( Index < NodePath.size() );

            return {};
        }

        std::pair<const node*, int>  findMesh(std::span<const std::string> NodePath, std::string_view MeshName) const noexcept
        {
            auto Pair = findNode(NodePath);
            if (Pair.first == nullptr) return {};

            for (auto mi : Pair.first->m_MeshList)
            {
                if ( m_MeshList[mi].m_Name == MeshName )
                    return { Pair.first, mi };
            }

            return {};
        }

        int findMesh(std::string_view MeshName) const noexcept
        {
            for (auto& m : m_MeshList)
            {
                if (m.m_Name == MeshName)
                    return static_cast<int>(&m - m_MeshList.data());
            }

            return -1;
        }


        std::vector<mesh>           m_MeshList;
        std::vector<std::string>    m_MaterialList;
        int                         m_NumFaces;
        node                        m_RootNode;

        XPROPERTY_DEF
        ( "details", details
        , obj_member<"RootNode",            &details::m_RootNode,       member_flags<flags::SHOW_READONLY> >
        , obj_member<"MeshList",            &details::m_MeshList,       member_flags<flags::SHOW_READONLY> >
        , obj_member<"NumFaces",            &details::m_NumFaces,       member_flags<flags::SHOW_READONLY> >
        , obj_member<"MaterialList",        &details::m_MaterialList,   member_flags<flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(details)
    XPROPERTY_REG2(mesh_, details::mesh)
    XPROPERTY_REG2(node_, details::node)
}

#endif
