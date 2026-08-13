#ifndef XGEOM_SKIN_XGPU_XRSC_LOADER_H
#define XGEOM_SKIN_XGPU_XRSC_LOADER_H
#pragma once

// This header file is used to provide a resource_guid for textures
#include "dependencies/xresource_mgr/source/xresource_mgr.h"

// All the information about the resource
namespace xrsc
{
    inline static constexpr auto    geom_skin_type_guid_v    = xresource::type_guid(xresource::guid_generator::Instance64FromString("GeomSkin"));
    using                           geom_skin                = xresource::def_guid<geom_skin_type_guid_v>;
}

// predefine the runtime struct... but we hide it to minimize header dependencies
namespace xgeom_skin::xgpu
{
    struct geom;
}

// Now we specify the loader and we must fill in all the information
template<>
struct xresource::loader< xrsc::geom_skin_type_guid_v >
{
    //--- Expected static parameters ---
    constexpr static inline auto            type_name_v         = L"GeomSkin";                 // This name is used to construct the path to the resource (if not provided)
    constexpr static inline auto            use_death_march_v   = false;                        // xGPU already has a death march implemented inside itself...
    using                                   data_type           = xgeom_skin::xgpu::geom;

    static data_type*                       Load        (xresource::mgr& Mgr, const full_guid& GUID);
    static void                             Destroy     (xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID);
};

#endif
