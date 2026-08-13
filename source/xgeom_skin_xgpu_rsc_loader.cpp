#include "xgeom_skin_xgpu_runtime.h"
#include "xgeom_skin_xgpu_rsc_loader.h"

#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"

//
// We will register the loader, the properties,
//
inline static auto s_GeomRegistrations = xresource::common_registrations<xrsc::geom_skin_type_guid_v>{};


//------------------------------------------------------------------

static
std::size_t MultipleOf64(std::size_t num)
{
    return (num + 63) & ~63;
}

//------------------------------------------------------------------

template<typename T>
std::size_t MakeMuultipleOf64(const T& X)
{
    return X.size();
    auto s = sizeof(X[0]);
    return MultipleOf64(s * X.size())/ s;
}

//------------------------------------------------------------------

xresource::loader< xrsc::geom_skin_type_guid_v >::data_type* xresource::loader< xrsc::geom_skin_type_guid_v >::Load(xresource::mgr& Mgr, const full_guid& GUID)
{
    auto&                   UserData    = Mgr.getUserData<resource_mgr_user_data>();
    std::wstring            Path        = Mgr.getResourcePath(GUID, type_name_v);
    xgeom_skin::geom*       pGeom       = nullptr;

    // Load the xgeom_skin
    xserializer::stream Stream;
    if (auto Err = Stream.Load(Path, pGeom); Err)
    {
        assert(false);
    }

    //
    // Time to copy the memory to the right places
    //

    // Upgrade to the runtime version
    xgeom_skin::xgpu::geom* pXGPUGeom = static_cast<xgeom_skin::xgpu::geom*>(pGeom);

    // Create buffers
    xgpu::device::error* p;

    // Ideally in the future all these buffer should be a single allocation...
    0
    ||(p = UserData.m_Device.Create(pXGPUGeom->VertexBuffer(),       xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX,  .m_EntryByteSize = (int)sizeof(xgeom_skin::geom::vertex),            .m_EntryCount = (int)pXGPUGeom->getVertices().size(),     .m_pData = pXGPUGeom->getVertices().data()}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->VertexExtrasBuffer(), xgpu::buffer::setup{.m_Type = xgpu::buffer::type::VERTEX,  .m_EntryByteSize = (int)sizeof(xgeom_skin::geom::vertex_extras),     .m_EntryCount = (int)pXGPUGeom->getVertexExtras().size(), .m_pData = pXGPUGeom->getVertexExtras().data()}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->IndexBuffer(),        xgpu::buffer::setup{.m_Type = xgpu::buffer::type::INDEX,   .m_EntryByteSize = (int)sizeof(std::uint16_t),                       .m_EntryCount = (int)pXGPUGeom->getIndices().size(),      .m_pData = pXGPUGeom->getIndices().data()}))
    ||(p = UserData.m_Device.Create(pXGPUGeom->ClusterBuffer(),      xgpu::buffer::setup{.m_Type = xgpu::buffer::type::STORAGE, .m_EntryByteSize = (int)sizeof(xgeom_skin::geom::cluster_data),      .m_EntryCount = (int)pXGPUGeom->getClusterData().size(),  .m_pData = pXGPUGeom->getClusterData().data() }))
    ;
    assert(p == nullptr);

    // Resolve the default material instances
    for (auto& E : pXGPUGeom->getDefaultMaterialInstances())
    {
        if (not E.empty())
        {
            if (auto pRsc = Mgr.getResource(E); pRsc == nullptr)
            {
                assert(false);
            }
        }
        else
        {
            // The user will have to deal with no-default materials...
        }
    }

    return pXGPUGeom;
}

//------------------------------------------------------------------

void xresource::loader< xrsc::geom_skin_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID)
{
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();

    // Release all the buffers (all 4 - xgeom_static's own loader omits ClusterBuffer here, which
    // looks like a leak; not propagating that omission into this new plugin)
    UserData.m_Device.Destroy(std::move(Data.VertexBuffer()));
    UserData.m_Device.Destroy(std::move(Data.VertexExtrasBuffer()));
    UserData.m_Device.Destroy(std::move(Data.IndexBuffer()));
    UserData.m_Device.Destroy(std::move(Data.ClusterBuffer()));

    // Release all the material instance references
    for (auto& E : Data.getDefaultMaterialInstances())
    {
        Mgr.ReleaseRef(E);
    }

    // Free the resource
    xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, &Data);
}
