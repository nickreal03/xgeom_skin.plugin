#ifndef XGEOM_SKIN_DESCRIPTOR_H
#define XGEOM_SKIN_DESCRIPTOR_H
#pragma once

#include "plugins/xmaterial_instance.plugin/source/xmaterial_instance_xgpu_rsc_loader.h"
#include "plugins/xskeleton.plugin/source/xskeleton_xgpu_rsc_loader.h"
#include "plugins/xskeleton.plugin/source/xskeleton_bone_manifest.h"
#include "xgeom_skin_details.h"

namespace xgeom_skin
{
    // While this should be just a type... it also happens to be an instance... the instance of the skin_geom_plugin
    // So while generating the type guid we must treat it as an instance.
    inline static constexpr auto    resource_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("GeomSkin"));
    static constexpr wchar_t        mesh_filter_v[] = L"Mesh\0 *.fbx; *.obj\0Any Thing\0 *.*\0";

    struct lod
    {
        float               m_LODReduction  = 0.7f;
        float               m_ScreenArea    = 1;            // in pixels
        XPROPERTY_DEF
        ( "lod", lod
        , obj_member<"LODReduction",    &lod::m_LODReduction >
        , obj_member<"ScreenArea",      &lod::m_ScreenArea >
        )
    };
    XPROPERTY_REG(lod)

    struct pre_transform
    {
        xmath::fvec3        m_Scale         = xmath::fvec3::fromOne();
        xmath::fvec3        m_Rotation      = xmath::fvec3::fromZero();
        xmath::fvec3        m_Translation   = xmath::fvec3::fromZero();

        XPROPERTY_DEF
        ( "preTransform", pre_transform
        , obj_member<"Scale",       &pre_transform::m_Scale >
        , obj_member<"Rotation",    &pre_transform::m_Rotation >
        , obj_member<"Translation", &pre_transform::m_Translation >
        )
    };
    XPROPERTY_REG(pre_transform)

    struct mesh_details
    {
        std::string                 m_Name = {};
        std::vector<lod>            m_LODs = {};

        XPROPERTY_DEF
        ( "mesh_details", mesh_details
        , obj_member<"Name", &mesh_details::m_Name >
        , obj_member<"LODs", &mesh_details::m_LODs >
        )
    };
    XPROPERTY_REG(mesh_details)

    using node_path = std::string;

    struct ungroup_mesh
    {
        node_path                   m_NodePath;         // The list of nodes that took to arrive to the mesh
        std::string                 m_MeshName;         // The actual mesh name
        mesh_details                m_MeshDetails;

        XPROPERTY_DEF
        ( "ungroup_mesh", ungroup_mesh
        , obj_member<"Node Path", &ungroup_mesh::m_NodePath, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Name", &ungroup_mesh::m_MeshName, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Details", &ungroup_mesh::m_MeshDetails, member_ui_open<true> >
        )
    };
    XPROPERTY_REG(ungroup_mesh)

    struct merge_group
    {
        std::string                 m_Name;
        std::vector<node_path>      m_NodePathList;
        mesh_details                m_MeshDetails;

        XPROPERTY_DEF
        ( "merge_group", merge_group
        , obj_member<"Name", &merge_group::m_Name >
        , obj_member<"Node Path List", &merge_group::m_NodePathList, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Details", &merge_group::m_MeshDetails >
        )
    };
    XPROPERTY_REG(merge_group)

    struct delete_entry
    {
        node_path                   m_NodePath;
        std::string                 m_MeshName;

        XPROPERTY_DEF
        ( "delete_entry", delete_entry
        , obj_member<"Node Path", &delete_entry::m_NodePath, member_flags< flags::SHOW_READONLY> >
        , obj_member<"Mesh Name", &delete_entry::m_MeshName, member_flags< flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(delete_entry)

    struct material_details
    {
        std::string                 m_Name;
        int                         m_RefCount = 0;
        XPROPERTY_DEF
        ( "material_details", material_details
        , obj_member<"Name", &material_details::m_Name, member_flags< flags::SHOW_READONLY> >
        , obj_member<"RefCount", &material_details::m_RefCount, member_flags< flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(material_details)

    //------------------------------------------------------------------------------------------------
    // HELPFUL FUNCTIONS
    //------------------------------------------------------------------------------------------------

    inline
    std::vector<std::string> SplitNodePath(std::string_view path) noexcept
    {
        std::vector<std::string> parts;
        std::string token;
        for (char c : path)
        {
            if (c == '/')
            {
                parts.push_back(std::move(token));
                token.clear();
            }
            else
            {
                token += c;
            }
        }
        if (!token.empty()) parts.push_back(std::move(token));
        return parts;
    }

    inline
    bool IsNodePrefixOf(std::string_view prefix, std::string_view full) noexcept
    {
        if (prefix.empty() && full.empty()) return true;
        if (prefix.empty()) return true;
        if (full.empty()) return false;
        if (!full.starts_with(prefix)) return false;
        size_t len = prefix.size();
        return len == full.size() || full[len] == '/';
    }

    inline
    std::string NodePathToString(const node_path& nodepath) noexcept
    {
        return nodepath;
    }

    //------------------------------------------------------------------------------------------------
    // DESCRIPTOR
    //------------------------------------------------------------------------------------------------
    struct descriptor : xresource_pipeline::descriptor::base
    {
        using parent = xresource_pipeline::descriptor::base;

        void SetupFromSource(std::string_view FileName) override
        {
        }

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
            //
            // A skin mesh without a skeleton is meaningless - required, just like xanim_package's own
            // skeleton reference.
            //
            if (m_SkeletonRef.empty())
            {
                Errors.push_back(std::format("The skeleton reference is NULL"));
            }

            //
            // Make sure tha the user has enter a file to import
            //
            if ( m_ImportAsset.empty() )
            {
                Errors.push_back(std::format("You have forgotten to fill the ImportMesh field which is required"));
            }

            //
            // Make sure all the group have valid names
            //
            if ( m_bMergeAllMeshes == false )
            {
                std::vector<std::string> Names;

                for( auto& Group : m_MergeGroupList)
                {
                    if ( Group.m_MeshDetails.m_Name.empty() )
                    {
                        Errors.push_back(std::format("Please enter tha valid MESH name for merge group {}", Group.m_MeshDetails.m_Name));
                    }
                }

                // Make sure group don't repeat mesh names
                for ( int i=0; i< m_MergeGroupList.size(); ++i )
                {
                    Names.push_back(m_MergeGroupList[i].m_MeshDetails.m_Name);

                    for (int j = i+1; j < m_MergeGroupList.size(); ++j)
                    {
                        if (m_MergeGroupList[i].m_MeshDetails.m_Name == m_MergeGroupList[j].m_MeshDetails.m_Name )
                        {
                            Errors.push_back(std::format("Please keep your mesh names unique because group {} and group {} are using {} as their mesh names", m_MergeGroupList[i].m_Name, m_MergeGroupList[j].m_Name, m_MergeGroupList[j].m_MeshDetails.m_Name));
                        }
                    }
                }

                // Double check the ungroup meshes
                for ( int i=0; i<m_UngroupMeshList.size(); i++)
                {
                    Names.push_back(m_UngroupMeshList[i].m_MeshDetails.m_Name);

                    for (int j = 0; j < m_UngroupMeshList.size(); j++)
                    {
                        if (m_UngroupMeshList[i].m_MeshDetails.m_Name == m_UngroupMeshList[j].m_MeshDetails.m_Name)
                        {
                            Errors.push_back(std::format("Please keep your mesh names unique because ungroup mesh {} and ungroup mesh {} are using {} as their mesh names", m_UngroupMeshList[i].m_MeshName, m_UngroupMeshList[j].m_MeshName, m_UngroupMeshList[j].m_MeshDetails.m_Name));
                        }

                    }
                }

                // Group names together
                std::sort(Names.begin(), Names.end());

                // Finally for a global name collision check
                for (int i = 0; i < Names.size()-1; i++)
                {
                    int iCollisions = 0;
                    for ( int j=i+1; Names[i] == Names[j]; j++ ) iCollisions++;

                    if (iCollisions)
                    {
                        Errors.push_back(std::format("Cross referencing names I found {} collisions with mesh name {}", iCollisions, Names[i]));
                        i += iCollisions;
                    }
                }
            }
            else
            {
                if ( m_AllMeshesDetails.m_Name.empty())
                {
                    Errors.push_back(std::format("You have forgotten to enter the name of the final mesh"));
                }
            }
        }

        int findMaterial(std::string_view Name)
        {
            for (auto& E : m_MaterialDetailsList)
                if (E.m_Name == Name) return static_cast<int>(&E - m_MaterialDetailsList.data());
            return -1;
        }

        bool isNodeInGroup(const node_path& nodepath) const
        {
            for (const auto& mg : m_MergeGroupList)
            {
                for (const auto& n : mg.m_NodePathList)
                {
                    if (IsNodePrefixOf(n, nodepath))
                        return true;
                }
            }
            return false;
        }

        bool isNodeInDeleteList(const node_path& nodepath) const
        {
            for (const auto& e : m_DeleteEntryList)
            {
                if (!e.m_MeshName.empty()) continue;
                if (IsNodePrefixOf(e.m_NodePath, nodepath))
                    return true;
            }
            return false;
        }

        bool isMeshInDeleteList(const node_path& nodepath, std::string_view MeshName) const
        {
            for (const auto& e : m_DeleteEntryList)
            {
                if (e.m_MeshName.empty())
                {
                    if (IsNodePrefixOf(e.m_NodePath, nodepath))
                        return true;
                }
                else
                {
                    if (e.m_NodePath == nodepath && e.m_MeshName == MeshName)
                        return true;
                }
            }
            return false;
        }

        std::pair<merge_group*, int> findMergeGroupFromNode(const node_path& nodepath)
        {
            for (auto& mg : m_MergeGroupList)
            {
                for (int i = 0; i < mg.m_NodePathList.size(); ++i)
                {
                    if (mg.m_NodePathList[i] == nodepath)
                    {
                        return { &mg, i };
                    }
                }
            }
            return {};
        }

        int findUngroupMesh(const node_path& nodepath, std::string_view MeshName) const
        {
            for (int i = 0; i < m_UngroupMeshList.size(); ++i)
            {
                if (m_UngroupMeshList[i].m_NodePath == nodepath && m_UngroupMeshList[i].m_MeshName == MeshName)
                    return i;
            }
            return -1;
        }

        void AddNodeInGroupList(merge_group& Group, const node_path& nodepath)
        {
            //
            // Make sure to remove it from any other group first
            //
            for (auto& g : m_MergeGroupList)
            {
                for (int i = 0; i < g.m_NodePathList.size(); ++i)
                {
                    if (g.m_NodePathList[i] == nodepath)
                    {
                        g.m_NodePathList.erase(g.m_NodePathList.begin() + i);
                        --i;
                    }
                }
            }

            //
            // Make sure that any meshes inside the ungroup section are removed
            //
            RemoveAllNodeMeshesFromUngroupList(nodepath);

            //
            // Add entry into new group
            //
            Group.m_NodePathList.push_back(nodepath);
        }

        void RemoveNodeFromGroup(merge_group& Group, int iNode, const details& Details)
        {
            //
            // Add meshes into unorder group
            //
            auto vecPath = SplitNodePath(Group.m_NodePathList[iNode]);
            auto DetailPair = Details.findNode(vecPath);
            assert(DetailPair.first);

            std::function<void(const details::node&, const node_path&)> Recursize = [&](const details::node& Node, const node_path& cpath)
            {
                // If this node is mark as deleted then we should skip everything...
                if (isNodeInDeleteList(cpath))
                    return;

                // Add all the meshes
                for (auto idx : Node.m_MeshList)
                {
                    m_UngroupMeshList.push_back({ cpath, Details.m_MeshList[idx].m_Name });
                }

                // Include children
                for (auto& c : Node.m_Children)
                {
                    node_path new_cpath = cpath.empty() ? c.m_Name : cpath + "/" + c.m_Name;
                    Recursize(c, new_cpath);
                }
            };
            Recursize(*DetailPair.first, Group.m_NodePathList[iNode]);

            //
            // Remove the Node from the actual group
            //
            Group.m_NodePathList.erase(Group.m_NodePathList.begin() + iNode);
        }

        void RemoveAllNodeMeshesFromUngroupList(const node_path& nodepath)
        {
            for (int i = 0; i < m_UngroupMeshList.size(); ++i)
            {
                if (IsNodePrefixOf(nodepath, m_UngroupMeshList[i].m_NodePath))
                {
                    m_UngroupMeshList.erase(m_UngroupMeshList.begin() + i);
                    --i;
                }
            }
        }

        void AddAllNodeMeshesToUngroupList(const node_path& nodepath, const details& Details)
        {
            //
            // Add meshes into unorder group
            //
            auto vecPath = SplitNodePath(nodepath);
            auto Pair = Details.findNode(vecPath);
            assert(Pair.first);

            std::function<void(const details::node&, const node_path&)> Recursize = [&](const details::node& Node, const node_path& cpath)
            {
                // Add all the meshes
                for (auto idx : Node.m_MeshList)
                {
                    m_UngroupMeshList.push_back({ cpath, Details.m_MeshList[idx].m_Name });
                }

                // Include children
                for (auto& c : Node.m_Children)
                {
                    node_path new_cpath = cpath.empty() ? c.m_Name : cpath + "/" + c.m_Name;
                    Recursize(c, new_cpath);
                }
            };
            Recursize(*Pair.first, nodepath);
        }

        void AddNodeInDeleteList(const node_path& nodepath, const details& Details)
        {
            auto Pair = findMergeGroupFromNode(nodepath);

            // If it is in a group remove it!
            if (Pair.first) RemoveNodeFromGroup(*Pair.first, Pair.second, Details);
            RemoveAllNodeMeshesFromUngroupList(nodepath);

            // Make sure it does not exists in the DeleteList
            if (isNodeInDeleteList(nodepath))
                return;

            // OK simply add it
            m_DeleteEntryList.push_back({ nodepath });

            //
            // Recompute the material references
            //
            RecomputeMaterialReferences(Details);
        }

        void RemoveNodeFromDeleteList(const node_path& nodepath, const details& Details)
        {
            //
            // Remove the Node from the undeleted list
            //
            for (int i = 0; i < m_DeleteEntryList.size(); ++i)
            {
                auto& t = m_DeleteEntryList[i];
                if (t.m_MeshName.empty() && t.m_NodePath == nodepath)
                {
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                    --i;
                }
            }

            //
            // Re-add all the Node meshes to the ungroup-list
            //
            if (!isNodeInGroup(nodepath))
            {
                AddAllNodeMeshesToUngroupList(nodepath, Details);
            }

            //
            // Recompute the material references
            //
            RecomputeMaterialReferences(Details);
        }

        void RecomputeMaterialReferences(const details& Details)
        {
            // clear out all the references
            for (auto& m : m_MaterialDetailsList) m.m_RefCount = 0;

            // Start adding references...
            std::function<void(const details::node&, const node_path&)> Recursize = [&](const details::node& Node, const node_path& cpath)
                {
                    // If this node is mark as deleted then we should skip everything...
                    if (isNodeInDeleteList(cpath))
                        return;

                    // Add all the meshes
                    for (auto idx : Node.m_MeshList)
                    {
                        for (auto MatIdx : Details.m_MeshList[idx].m_MaterialList)
                        {
                            auto Index = findMaterial(Details.m_MaterialList[MatIdx]);
                            m_MaterialDetailsList[Index].m_RefCount++;
                        }
                    }

                    // Include children
                    for (auto& c : Node.m_Children)
                    {
                        node_path new_cpath = cpath.empty() ? c.m_Name : cpath + "/" + c.m_Name;
                        Recursize(c, new_cpath);
                    }
                };
            Recursize(Details.m_RootNode, Details.m_RootNode.m_Name);
        }

        // Mirrors xanim_package_desc::descriptor::getSkeletonBoneManifestPath() exactly - same "find
        // the project root, then the standard GUID-sharding formula" convention, pointed at the
        // referenced Skeleton's own log folder. "Skeleton" here is a hardcoded literal matching that
        // plugin's own fixed PipelinePlugin/TypeName; the manifest file itself is consumer-agnostic
        // despite its "AnimPackage.txt" name.
        xerr getSkeletonBoneManifestPath(std::wstring& Path, std::wstring_view DescriptorFileName) const noexcept
        {
            auto Pos = DescriptorFileName.rfind(L".lionprj");
            if (Pos != std::wstring_view::npos) Pos += sizeof(".lionprj");
            else if (Pos = DescriptorFileName.rfind(L".lionlib"); Pos != std::wstring_view::npos) Pos += sizeof(".lionlib");
            else return xerr::create_f<xresource_pipeline::state, "Unable to locate the project path, fail to load the referenced skeleton's bone manifest">();

            const auto ProjectPath = DescriptorFileName.substr(0, Pos - 1);
            Path = std::format(L"{}/Cache/Resources/Logs/Skeleton/{:02X}/{:02X}/{:X}.log/AnimPackage.txt"
                , ProjectPath
                , m_SkeletonRef.m_Instance.m_Value & 0xff
                , (m_SkeletonRef.m_Instance.m_Value & 0xff00) >> 8
                , m_SkeletonRef.m_Instance.m_Value
            );

            return {};
        }

        xerr Serialize(bool isReading, std::wstring_view FileName, xproperty::settings::context& Context) noexcept override
        {
            if (auto Err = xresource_pipeline::descriptor::base::Serialize(isReading, FileName, Context); Err)
                return Err;

            if (not isReading) return {};
            if (m_SkeletonRef.empty()) return {};

            // Cross-plugin dependency: read the Skeleton compiler's own bone-manifest export (its
            // final, post-merge bone order/hashes) - not its compiled binary. RunAfter in this
            // plugin's Plugin.config guarantees this file already exists by the time we get here.
            std::wstring ManifestPath;
            if (auto Err = getSkeletonBoneManifestPath(ManifestPath, FileName); Err)
                return Err;

            xtextfile::stream                File;
            xproperty::settings::context    C;

            if (auto Err = File.Open(true, ManifestPath, xtextfile::file_type::TEXT); Err)
                return xerr::create_f<xresource_pipeline::state, "Error opening the referenced skeleton's bone manifest - has it been compiled yet?">(Err);

            if (auto Err = xproperty::sprop::serializer::Stream(File, m_ResolvedSkeletonBones, C); Err)
                return xerr::create_f<xresource_pipeline::state, "Error reading the referenced skeleton's bone manifest">(Err);

            return {};
        }

        inline std::vector<std::string> MergeWithDetails(xgeom_skin::details& Details);

        std::wstring                                m_ImportAsset           = {};
        pre_transform                               m_PreTranslation        = {};
        bool                                        m_bMergeAllMeshes       = true;
        mesh_details                                m_AllMeshesDetails      = {};
        std::vector<material_details>               m_MaterialDetailsList   = {};
        std::vector<xrsc::material_instance_ref>    m_MaterialInstRefList   = {};
        std::vector<ungroup_mesh>                   m_UngroupMeshList       = {};
        std::vector<merge_group>                    m_MergeGroupList        = {};
        std::vector<delete_entry>                   m_DeleteEntryList       = {};
        xrsc::skeleton                               m_SkeletonRef           = {};   // required - Validate() errors if empty
        xskeleton_desc::bone_manifest                m_ResolvedSkeletonBones = {};   // NOT saved - populated in Serialize() above

        XPROPERTY_VDEF
        ( "GeomSkin", descriptor
            , obj_member<"ImportAsset",         &descriptor::m_ImportAsset, member_ui<std::wstring>::file_dialog<mesh_filter_v, true, 1> >
            , obj_member<"SkeletonRef",         &descriptor::m_SkeletonRef >
            , obj_member<"PreTranslation",      &descriptor::m_PreTranslation >
            , obj_member<"bMergeAllMeshes",     &descriptor::m_bMergeAllMeshes >
            , obj_member<"AllMeshesDetails",    &descriptor::m_AllMeshesDetails, member_ui_open<true>, member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags = {};
                Flags.m_bDontShow = !O.m_bMergeAllMeshes;
                return Flags;
            }
            >>
            , obj_member < "Merge Group List", &descriptor::m_MergeGroupList, member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags = {};
                Flags.m_bDontShow = O.m_bMergeAllMeshes;
                return Flags;
            }
            >>
            , obj_member < "Ungroup Mesh List", &descriptor::m_UngroupMeshList, member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags = {};
                Flags.m_bDontShow = O.m_bMergeAllMeshes;
                return Flags;
            }
            >>
            , obj_member<"Deleted List", &descriptor::m_DeleteEntryList >
            , obj_member<"MaterialDetailsList", &descriptor::m_MaterialDetailsList, member_flags<flags::DONT_SHOW>>
            , obj_member<"MaterialInstance", &descriptor::m_MaterialInstRefList, member_ui_open<true> >
            , obj_member<"ResolvedSkeletonBones", &descriptor::m_ResolvedSkeletonBones, member_flags<flags::DONT_SHOW, flags::DONT_SAVE> >
        )
    };
    XPROPERTY_VREG(descriptor)

    //--------------------------------------------------------------------------------------

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor(void) const noexcept override
        {
            return std::make_unique<descriptor>();
        };

        xresource::type_guid ResourceTypeGUID(void) const noexcept override
        {
            return resource_type_guid_v;
        }

        const char* ResourceTypeName(void) const noexcept override
        {
            return "GeomSkin";
        }

        const xproperty::type::object& ResourceXPropertyObject(void) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };

    inline static factory g_Factory{};

    //--------------------------------------------------------------------------------------
    inline
        std::vector<std::string> descriptor::MergeWithDetails(xgeom_skin::details& Details)
    {
        std::vector<std::string> Messages;

        //------------------------------------------------------------------------------------------
        // Try to remove entries from our list base on the new state of details...
        //------------------------------------------------------------------------------------------

        //
        // Merge with Merge-Group-List
        //
        for (auto& g : m_MergeGroupList)
        {
            for (int i = 0; i < g.m_NodePathList.size(); ++i)
            {
                auto vec = SplitNodePath(g.m_NodePathList[i]);
                if (auto Pair = Details.findNode(vec); Pair.first)
                {
                    // Great we still have the node...
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a node [{}] We will have to remove it from the Merge group list", g.m_NodePathList[i]));

                    // We no longer have this node!
                    g.m_NodePathList.erase(g.m_NodePathList.begin() + i);
                    --i;
                }
            }
        }

        //
        // Merge the Ungroup mesh list
        //
        for (int i = 0; i < m_UngroupMeshList.size(); ++i)
        {
            auto vec = SplitNodePath(m_UngroupMeshList[i].m_NodePath);
            if (auto Pair = Details.findMesh(vec, m_UngroupMeshList[i].m_MeshName); Pair.first)
            {
                // Great we still have our mesh!
            }
            else
            {

                // OK can we find if the mesh still exist at all...
                if (int Idx = Details.findMesh(m_UngroupMeshList[i].m_MeshName); Idx == -1)
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] who used to be in this node [{}]. The mesh have been completly removed. We will remove from the ungrouped mesh list."
                        , m_UngroupMeshList[i].m_MeshName, m_UngroupMeshList[i].m_NodePath));
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] who used to be in this node [{}]. We will remove from the ungrouped mesh list."
                        , m_UngroupMeshList[i].m_MeshName, m_UngroupMeshList[i].m_NodePath));
                }

                // Remove it from our list
                m_UngroupMeshList.erase(m_UngroupMeshList.begin() + i);
                --i;
            }
        }

        //
        // Merge Meshes to be deleted...
        //
        for (int i = 0; i < m_DeleteEntryList.size(); ++i)
        {
            // Check the case where we are deleting nodes
            if (not m_DeleteEntryList[i].m_NodePath.empty() && m_DeleteEntryList[i].m_MeshName.empty())
            {
                auto vec = SplitNodePath(m_DeleteEntryList[i].m_NodePath);
                if (auto Pair = Details.findNode(vec); Pair.first)
                {
                    // Great we still have the node
                }
                else
                {
                    Messages.push_back(std::format("WARNING: We not longer found node [{}] we will remove it from the Delte Entry List."
                        , m_DeleteEntryList[i].m_NodePath));

                    // We do not have the node so we will remove it from our list
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                    --i;
                }
            }
            // Check the case where we are deleting meshes
            else if (not m_DeleteEntryList[i].m_NodePath.empty() && not m_DeleteEntryList[i].m_MeshName.empty())
            {
                auto vec = SplitNodePath(m_DeleteEntryList[i].m_NodePath);
                if (auto Pair = Details.findMesh(vec, m_DeleteEntryList[i].m_MeshName); Pair.first)
                {
                    // Great we still have the mesh
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] who used to be in this node [{}]. We will remove from the delete entry list."
                        , m_DeleteEntryList[i].m_MeshName, m_DeleteEntryList[i].m_NodePath));

                    // We do not have the node so we will remove it from our list
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                    --i;
                }
            }
            // Check the case where we are deleteting all the mesh references
            else if (m_DeleteEntryList[i].m_NodePath.empty() && not m_DeleteEntryList[i].m_MeshName.empty())
            {
                if (auto idx = Details.findMesh(m_DeleteEntryList[i].m_MeshName); idx != -1)
                {
                    // Great we still removing all these meshes
                }
                else
                {
                    Messages.push_back(std::format("WARNING: Failed to find a mesh [{}] so We will remove from the delete entry list.", m_DeleteEntryList[i].m_MeshName));

                    // We do not have the node so we will remove it from our list
                    m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                    --i;
                }
            }
            else
            {
                // We should not have such case...
                m_DeleteEntryList.erase(m_DeleteEntryList.begin() + i);
                --i;
            }
        }

        //------------------------------------------------------------------------------------------
        // Try to add any new mesh from details that is currently not in our list of ungroup meshes
        //------------------------------------------------------------------------------------------
        {
            std::function<void(const details::node&, const node_path&)> Recurse = [&](const details::node& Node, const node_path& parent_path)
                {
                    node_path new_path = parent_path.empty() ? Node.m_Name : parent_path + "/" + Node.m_Name;

                    // This path is already part of the group so just skip everything
                    if (findMergeGroupFromNode(new_path).first != nullptr)
                        return;

                    // Add meshes from this node
                    for (auto idx : Node.m_MeshList)
                    {
                        if (findUngroupMesh(new_path, Details.m_MeshList[idx].m_Name) == -1)
                        {
                            // OK we do not have this mesh...
                            // Check if we may have mark it for deletion if not it must be a new mesh
                            if (!isMeshInDeleteList(new_path, Details.m_MeshList[idx].m_Name))
                                m_UngroupMeshList.push_back({ new_path, Details.m_MeshList[idx].m_Name });
                        }
                        else
                        {
                            // We have this mesh alreay... so nothing to do...
                        }
                    }

                    // OK let the children add meshes if they need
                    for (auto& c : Node.m_Children)
                    {
                        Recurse(c, new_path);
                    }
                };

            //
            // Add all the ungroup meshes into our list
            //
            node_path parent_path;
            Recurse(Details.m_RootNode, parent_path);
        }

        //---------------------------------------------------------------------------------------------
        // Update the materials
        //---------------------------------------------------------------------------------------------

        // First remove any materials no longer in used
        for (int i = 0; i < m_MaterialDetailsList.size(); ++i)
        {
            if (Details.findMaterial(m_MaterialDetailsList[i].m_Name) == -1)
            {
                m_MaterialDetailsList.erase(m_MaterialDetailsList.begin() + i);
                m_MaterialInstRefList.erase(m_MaterialInstRefList.begin() + i);
                --i;
            }
        }

        // Add all new Materials if we have to...
        for (auto& E : Details.m_MaterialList)
        {
            if (auto Index = findMaterial(E); Index == -1)
            {
                m_MaterialDetailsList.emplace_back(E);
                m_MaterialInstRefList.emplace_back();
            }
        }

        // Recompute material references
        RecomputeMaterialReferences(Details);


        return Messages;
    }
}
#endif
