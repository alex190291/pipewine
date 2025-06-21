/*
 * Copyright (C) 2006 Robert Reif
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winreg.h>
#include <objbase.h>
#include <unknwn.h>

#include "driver_clsid.h"

#ifdef DEBUG
#include "wine/debug.h"
#endif
/* WINE_DEFAULT_DEBUG_CHANNEL(asio); */


typedef struct {
    const IClassFactoryVtbl * lpVtbl;
    LONG ref;
} IClassFactoryImpl;

extern HRESULT WINAPI WineASIOCreateInstance(REFIID riid, LPVOID *ppobj, IUnknown *cls_factory);

/*******************************************************************************
 * ClassFactory
 */

static HRESULT WINAPI CF_QueryInterface(LPCLASSFACTORY iface, REFIID riid, LPVOID *ppobj)
{
    /* IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    FIXME("(%p, %s, %p) stub!\n", This, debugstr_guid(riid), ppobj); */
    if (ppobj == NULL)
        return E_POINTER;
    return E_NOINTERFACE;
}

static ULONG WINAPI CF_AddRef(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    ULONG ref = InterlockedIncrement(&(This->ref));
    /* TRACE("iface: %p, ref has been set to %x\n", This, ref); */
    return ref;
}

static ULONG WINAPI CF_Release(LPCLASSFACTORY iface)
{
    IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    ULONG ref = InterlockedDecrement(&(This->ref));
    /* TRACE("iface %p, ref has been set to %x\n", This, ref); */
    /* static class, won't be freed */
    return ref;
}

static HRESULT WINAPI CF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj);

static HRESULT WINAPI CF_LockServer(LPCLASSFACTORY iface, BOOL dolock)
{
    /* IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    FIXME("iface: %p, dolock: %d) stub!\n", This, dolock); */
    return E_NOTIMPL;
}

static const IClassFactoryVtbl CF_Vtbl = {
    CF_QueryInterface,
    CF_AddRef,
    CF_Release,
    CF_CreateInstance,
    CF_LockServer
};

static IClassFactoryImpl WINEASIO_CF = { &CF_Vtbl, 1 };

static HRESULT WINAPI CF_CreateInstance(LPCLASSFACTORY iface, LPUNKNOWN pOuter, REFIID riid, LPVOID *ppobj)
{
    /* IClassFactoryImpl *This = (IClassFactoryImpl *)iface;
    TRACE("iface: %p, pOuter: %p, riid: %s, ppobj: %p)\n", This, pOuter, debugstr_guid(riid), ppobj); */

    if (pOuter)
        return CLASS_E_NOAGGREGATION;

    if (ppobj == NULL) {
        /* WARN("invalid parameter\n"); */
        return E_INVALIDARG;
    }

    *ppobj = NULL;
    /* TRACE("Creating the WineASIO object\n"); */
    return WineASIOCreateInstance(riid, ppobj, (IUnknown *)&WINEASIO_CF);
}

/*******************************************************************************
 * DllGetClassObject [DSOUND.@]
 * Retrieves class object from a DLL object
 *
 * NOTES
 *    Docs say returns STDAPI
 *
 * PARAMS
 *    rclsid [I] CLSID for the class object
 *    riid   [I] Reference to identifier of interface for class object
 *    ppv    [O] Address of variable to receive interface pointer for riid
 *
 * RETURNS
 *    Success: S_OK
 *    Failure: CLASS_E_CLASSNOTAVAILABLE, E_OUTOFMEMORY, E_INVALIDARG,
 *             E_UNEXPECTED
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    /* TRACE("rclsid: %s, riid: %s, ppv: %p)\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv); */

    if (ppv == NULL) {
        /* WARN("invalid parameter\n"); */
        return E_INVALIDARG;
    }

    *ppv = NULL;

    if (!IsEqualIID(riid, &IID_IClassFactory) && !IsEqualIID(riid, &IID_IUnknown))
    {
        /* WARN("no interface for %s\n", debugstr_guid(riid)); */
        return E_NOINTERFACE;
    }

    if (IsEqualGUID(rclsid, &CLSID_PipeWine))
    {
        CF_AddRef((IClassFactory*) &WINEASIO_CF);
        *ppv = &WINEASIO_CF;
        return S_OK;
    }

    /* WARN("rclsid: %s, riid: %s, ppv: %p): no class found.\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv); */
    return CLASS_E_CLASSNOTAVAILABLE;
}


/*******************************************************************************
 * DllCanUnloadNow
 * Determines whether the DLL is in use.
 *
 * RETURNS
 *    Success: S_OK
 *    Failure: S_FALSE
 */
HRESULT WINAPI DllCanUnloadNow(void)
{
    return WINEASIO_CF.ref == 1 ? S_OK : S_FALSE;
}

/***********************************************************************
 *           DllMain (ASIO.init)
 */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    /* TRACE("hInstDLL: %p, fdwReason: %x lpvReserved: %p)\n", hInstDLL, fdwReason, lpvReserved); */

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
/*        TRACE("DLL_PROCESS_ATTACH\n"); */
        break;
    case DLL_PROCESS_DETACH:
/*        TRACE("DLL_PROCESS_DETACH\n"); */
        break;
    case DLL_THREAD_ATTACH:
/*        TRACE("DLL_THREAD_ATTACH\n"); */
        break;
    case DLL_THREAD_DETACH:
/*        TRACE("DLL_THREAD_DETACH\n"); */
        break;
    default:
/*        TRACE("UNKNOWN REASON\n"); */
        break;
    }
    return TRUE;
}

// Configuration utility functions are now in pw_config_utils.c
