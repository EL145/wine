/* OLE DB Conversion library
 *
 * Copyright 2009 Huw Davies
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

#define COBJMACROS
#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "ole2.h"
#include "msdadc.h"
#include "oledberr.h"

#include "oledb_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(oledb);

typedef struct
{
    const struct IDataConvertVtbl *lpVtbl;
    const struct IDCInfoVtbl *lpDCInfoVtbl;

    LONG ref;

    UINT version; /* Set by IDCInfo_SetInfo */
} convert;

static inline convert *impl_from_IDataConvert(IDataConvert *iface)
{
    return (convert *)((char*)iface - FIELD_OFFSET(convert, lpVtbl));
}

static inline convert *impl_from_IDCInfo(IDCInfo *iface)
{
    return (convert *)((char*)iface - FIELD_OFFSET(convert, lpDCInfoVtbl));
}

static HRESULT WINAPI convert_QueryInterface(IDataConvert* iface,
                                             REFIID riid,
                                             void **obj)
{
    convert *This = impl_from_IDataConvert(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), obj);

    *obj = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
       IsEqualIID(riid, &IID_IDataConvert))
    {
        *obj = iface;
    }
    else if(IsEqualIID(riid, &IID_IDCInfo))
    {
        *obj = &This->lpDCInfoVtbl;
    }
    else
    {
        FIXME("interface %s not implemented\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IDataConvert_AddRef(iface);
    return S_OK;
}


static ULONG WINAPI convert_AddRef(IDataConvert* iface)
{
    convert *This = impl_from_IDataConvert(iface);
    TRACE("(%p)\n", This);

    return InterlockedIncrement(&This->ref);
}


static ULONG WINAPI convert_Release(IDataConvert* iface)
{
    convert *This = impl_from_IDataConvert(iface);
    LONG ref;

    TRACE("(%p)\n", This);

    ref = InterlockedDecrement(&This->ref);
    if(ref == 0)
    {
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

static int get_length(DBTYPE type)
{
    switch(type)
    {
    case DBTYPE_I1:
    case DBTYPE_UI1:
        return 1;
    case DBTYPE_I2:
    case DBTYPE_UI2:
        return 2;
    case DBTYPE_I4:
    case DBTYPE_UI4:
        return 4;
    case DBTYPE_I8:
    case DBTYPE_UI8:
        return 8;
    case DBTYPE_BSTR:
        return sizeof(BSTR);
    case DBTYPE_WSTR:
        return 0;
    default:
        FIXME("Unhandled type %04x\n", type);
        return 0;
    }
}

static HRESULT WINAPI convert_DataConvert(IDataConvert* iface,
                                          DBTYPE src_type, DBTYPE dst_type,
                                          DBLENGTH src_len, DBLENGTH *dst_len,
                                          void *src, void *dst,
                                          DBLENGTH dst_max_len,
                                          DBSTATUS src_status, DBSTATUS *dst_status,
                                          BYTE precision, BYTE scale,
                                          DBDATACONVERT flags)
{
    convert *This = impl_from_IDataConvert(iface);
    HRESULT hr;

    TRACE("(%p)->(%d, %d, %d, %p, %p, %p, %d, %d, %p, %d, %d, %x)\n", This,
          src_type, dst_type, src_len, dst_len, src, dst, dst_max_len,
          src_status, dst_status, precision, scale, flags);

    *dst_len = get_length(dst_type);
    *dst_status = DBSTATUS_E_BADACCESSOR;

    if(IDataConvert_CanConvert(iface, src_type, dst_type) != S_OK)
    {
        return DB_E_UNSUPPORTEDCONVERSION;
    }

    if(src_type == DBTYPE_STR)
    {
        BSTR b;
        DWORD len;

        if(flags & DBDATACONVERT_LENGTHFROMNTS)
            len = MultiByteToWideChar(CP_ACP, 0, src, -1, NULL, 0) - 1;
        else
            len = MultiByteToWideChar(CP_ACP, 0, src, src_len, NULL, 0);
        b = SysAllocStringLen(NULL, len);
        if(!b) return E_OUTOFMEMORY;
        if(flags & DBDATACONVERT_LENGTHFROMNTS)
            MultiByteToWideChar(CP_ACP, 0, src, -1, b, len + 1);
        else
            MultiByteToWideChar(CP_ACP, 0, src, src_len, b, len);

        hr = IDataConvert_DataConvert(iface, DBTYPE_BSTR, dst_type, 0, dst_len,
                                      &b, dst, dst_max_len, src_status, dst_status,
                                      precision, scale, flags);

        SysFreeString(b);
        return hr;
    }

    if(src_type == DBTYPE_WSTR)
    {
        BSTR b;

        if(flags & DBDATACONVERT_LENGTHFROMNTS)
            b = SysAllocString(src);
        else
            b = SysAllocStringLen(src, src_len / 2);
        if(!b) return E_OUTOFMEMORY;
        hr = IDataConvert_DataConvert(iface, DBTYPE_BSTR, dst_type, 0, dst_len,
                                      &b, dst, dst_max_len, src_status, dst_status,
                                      precision, scale, flags);
        SysFreeString(b);
        return hr;
    }

    switch(dst_type)
    {
    case DBTYPE_I2:
    {
        signed short *d = dst;
        switch(src_type)
        {
        case DBTYPE_EMPTY:       *d = 0; hr = S_OK;                              break;
        case DBTYPE_I2:          *d = *(signed short*)src; hr = S_OK;            break;
        case DBTYPE_I4:          hr = VarI2FromI4(*(signed int*)src, d);         break;
        case DBTYPE_R4:          hr = VarI2FromR4(*(FLOAT*)src, d);              break;
        case DBTYPE_R8:          hr = VarI2FromR8(*(double*)src, d);             break;
        case DBTYPE_CY:          hr = VarI2FromCy(*(CY*)src, d);                 break;
        case DBTYPE_DATE:        hr = VarI2FromDate(*(DATE*)src, d);             break;
        case DBTYPE_BSTR:        hr = VarI2FromStr(*(WCHAR**)src, LOCALE_USER_DEFAULT, 0, d); break;
        case DBTYPE_BOOL:        hr = VarI2FromBool(*(VARIANT_BOOL*)src, d);     break;
        case DBTYPE_DECIMAL:     hr = VarI2FromDec((DECIMAL*)src, d);            break;
        case DBTYPE_I1:          hr = VarI2FromI1(*(signed char*)src, d);        break;
        case DBTYPE_UI1:         hr = VarI2FromUI1(*(BYTE*)src, d);              break;
        case DBTYPE_UI2:         hr = VarI2FromUI2(*(WORD*)src, d);              break;
        case DBTYPE_UI4:         hr = VarI2FromUI4(*(DWORD*)src, d);             break;
        case DBTYPE_I8:          hr = VarI2FromI8(*(LONGLONG*)src, d);           break;
        case DBTYPE_UI8:         hr = VarI2FromUI8(*(ULONGLONG*)src, d);         break;
        default: FIXME("Unimplemented conversion %04x -> I2\n", src_type); return E_NOTIMPL;
        }
        break;
    }

    case DBTYPE_I4:
    {
        signed int *d = dst;
        switch(src_type)
        {
        case DBTYPE_EMPTY:       *d = 0; hr = S_OK;                              break;
        case DBTYPE_I2:          hr = VarI4FromI2(*(signed short*)src, d);       break;
        case DBTYPE_I4:          *d = *(signed int*)src; hr = S_OK;              break;
        case DBTYPE_R4:          hr = VarI4FromR4(*(FLOAT*)src, d);              break;
        case DBTYPE_R8:          hr = VarI4FromR8(*(double*)src, d);             break;
        case DBTYPE_CY:          hr = VarI4FromCy(*(CY*)src, d);                 break;
        case DBTYPE_DATE:        hr = VarI4FromDate(*(DATE*)src, d);             break;
        case DBTYPE_BSTR:        hr = VarI4FromStr(*(WCHAR**)src, LOCALE_USER_DEFAULT, 0, d); break;
        case DBTYPE_BOOL:        hr = VarI4FromBool(*(VARIANT_BOOL*)src, d);     break;
        case DBTYPE_DECIMAL:     hr = VarI4FromDec((DECIMAL*)src, d);            break;
        case DBTYPE_I1:          hr = VarI4FromI1(*(signed char*)src, d);        break;
        case DBTYPE_UI1:         hr = VarI4FromUI1(*(BYTE*)src, d);              break;
        case DBTYPE_UI2:         hr = VarI4FromUI2(*(WORD*)src, d);              break;
        case DBTYPE_UI4:         hr = VarI4FromUI4(*(DWORD*)src, d);             break;
        case DBTYPE_I8:          hr = VarI4FromI8(*(LONGLONG*)src, d);           break;
        case DBTYPE_UI8:         hr = VarI4FromUI8(*(ULONGLONG*)src, d);         break;
        default: FIXME("Unimplemented conversion %04x -> I4\n", src_type); return E_NOTIMPL;
        }
        break;
    }

    case DBTYPE_BSTR:
    {
        BSTR *d = dst;
        switch(src_type)
        {
        case DBTYPE_EMPTY:       *d = SysAllocStringLen(NULL, 0); hr = *d ? S_OK : E_OUTOFMEMORY;      break;
        case DBTYPE_I2:          hr = VarBstrFromI2(*(signed short*)src, LOCALE_USER_DEFAULT, 0, d);   break;
        case DBTYPE_I4:          hr = VarBstrFromI4(*(signed int*)src, LOCALE_USER_DEFAULT, 0, d);     break;
        case DBTYPE_R4:          hr = VarBstrFromR4(*(FLOAT*)src, LOCALE_USER_DEFAULT, 0, d);          break;
        case DBTYPE_R8:          hr = VarBstrFromR8(*(double*)src, LOCALE_USER_DEFAULT, 0, d);         break;
        case DBTYPE_CY:          hr = VarBstrFromCy(*(CY*)src, LOCALE_USER_DEFAULT, 0, d);             break;
        case DBTYPE_DATE:        hr = VarBstrFromDate(*(DATE*)src, LOCALE_USER_DEFAULT, 0, d);         break;
        case DBTYPE_BSTR:        *d = SysAllocStringLen(*(BSTR*)src, SysStringLen(*(BSTR*)src)); hr = *d ? S_OK : E_OUTOFMEMORY;     break;
        case DBTYPE_BOOL:        hr = VarBstrFromBool(*(VARIANT_BOOL*)src, LOCALE_USER_DEFAULT, 0, d); break;
        case DBTYPE_DECIMAL:     hr = VarBstrFromDec((DECIMAL*)src, LOCALE_USER_DEFAULT, 0, d);        break;
        case DBTYPE_I1:          hr = VarBstrFromI1(*(signed char*)src, LOCALE_USER_DEFAULT, 0, d);    break;
        case DBTYPE_UI1:         hr = VarBstrFromUI1(*(BYTE*)src, LOCALE_USER_DEFAULT, 0, d);          break;
        case DBTYPE_UI2:         hr = VarBstrFromUI2(*(WORD*)src, LOCALE_USER_DEFAULT, 0, d);          break;
        case DBTYPE_UI4:         hr = VarBstrFromUI4(*(DWORD*)src, LOCALE_USER_DEFAULT, 0, d);         break;
        case DBTYPE_I8:          hr = VarBstrFromI8(*(LONGLONG*)src, LOCALE_USER_DEFAULT, 0, d);       break;
        case DBTYPE_UI8:         hr = VarBstrFromUI8(*(ULONGLONG*)src, LOCALE_USER_DEFAULT, 0, d);     break;
        default: FIXME("Unimplemented conversion %04x -> BSTR\n", src_type); return E_NOTIMPL;
        }
        break;
    }

    case DBTYPE_WSTR:
    {
        BSTR b;
        DBLENGTH bstr_len;
        INT bytes_to_copy;
        hr = IDataConvert_DataConvert(iface, src_type, DBTYPE_BSTR, src_len, &bstr_len,
                                      src, &b, sizeof(BSTR), src_status, dst_status,
                                      precision, scale, flags);
        if(hr != S_OK) return hr;
        bstr_len = SysStringLen(b);
        *dst_len = bstr_len * sizeof(WCHAR); /* Doesn't include size for '\0' */
        *dst_status = DBSTATUS_S_OK;
        bytes_to_copy = min(*dst_len + sizeof(WCHAR), dst_max_len);
        if(dst)
        {
            if(bytes_to_copy >= sizeof(WCHAR))
            {
                memcpy(dst, b, bytes_to_copy - sizeof(WCHAR));
                *((WCHAR*)dst + bytes_to_copy / sizeof(WCHAR) - 1) = 0;
                if(bytes_to_copy < *dst_len + sizeof(WCHAR))
                    *dst_status = DBSTATUS_S_TRUNCATED;
            }
            else
            {
                *dst_status = DBSTATUS_E_DATAOVERFLOW;
                hr = DB_E_ERRORSOCCURRED;
            }
        }
        SysFreeString(b);
        return hr;
    }

    default:
        FIXME("Unimplemented conversion %04x -> %04x\n", src_type, dst_type);
        return E_NOTIMPL;

    }

    if(hr == DISP_E_OVERFLOW)
    {
        *dst_status = DBSTATUS_E_DATAOVERFLOW;
        hr = DB_E_ERRORSOCCURRED;
    }
    else if(hr == S_OK)
        *dst_status = DBSTATUS_S_OK;

    return hr;
}

static inline WORD get_dbtype_class(DBTYPE type)
{
    switch(type)
    {
    case DBTYPE_I2:
    case DBTYPE_R4:
    case DBTYPE_R8:
    case DBTYPE_I1:
    case DBTYPE_UI1:
    case DBTYPE_UI2:
        return DBTYPE_I2;

    case DBTYPE_I4:
    case DBTYPE_UI4:
        return DBTYPE_I4;

    case DBTYPE_I8:
    case DBTYPE_UI8:
        return DBTYPE_I8;

    case DBTYPE_BSTR:
    case DBTYPE_STR:
    case DBTYPE_WSTR:
        return DBTYPE_BSTR;

    case DBTYPE_DBDATE:
    case DBTYPE_DBTIME:
    case DBTYPE_DBTIMESTAMP:
        return DBTYPE_DBDATE;
    }
    return type;
}

/* Many src types will convert to this group of dst types */
static inline BOOL common_class(WORD dst_class)
{
    switch(dst_class)
    {
    case DBTYPE_EMPTY:
    case DBTYPE_NULL:
    case DBTYPE_I2:
    case DBTYPE_I4:
    case DBTYPE_BSTR:
    case DBTYPE_BOOL:
    case DBTYPE_VARIANT:
    case DBTYPE_I8:
    case DBTYPE_CY:
    case DBTYPE_DECIMAL:
    case DBTYPE_NUMERIC:
        return TRUE;
    }
    return FALSE;
}

static inline BOOL array_type(DBTYPE type)
{
    return (type >= DBTYPE_I2 && type <= DBTYPE_UI4);
}

static HRESULT WINAPI convert_CanConvert(IDataConvert* iface,
                                         DBTYPE src_type, DBTYPE dst_type)
{
    convert *This = impl_from_IDataConvert(iface);
    DBTYPE src_base_type = src_type & 0x1ff;
    DBTYPE dst_base_type = dst_type & 0x1ff;
    WORD dst_class = get_dbtype_class(dst_base_type);

    TRACE("(%p)->(%d, %d)\n", This, src_type, dst_type);

    if(src_type & DBTYPE_VECTOR || dst_type & DBTYPE_VECTOR) return S_FALSE;

    if(src_type & DBTYPE_ARRAY)
    {
        if(!array_type(src_base_type)) return S_FALSE;
        if(dst_type & DBTYPE_ARRAY)
        {
            if(src_type == dst_type) return S_OK;
            return S_FALSE;
        }
        if(dst_type == DBTYPE_VARIANT) return S_OK;
        return S_FALSE;
    }

    if(dst_type & DBTYPE_ARRAY)
    {
        if(!array_type(dst_base_type)) return S_FALSE;
        if(src_type == DBTYPE_IDISPATCH || src_type == DBTYPE_VARIANT) return S_OK;
        return S_FALSE;
    }

    if(dst_type & DBTYPE_BYREF)
        if(dst_base_type != DBTYPE_BYTES && dst_base_type != DBTYPE_STR && dst_base_type != DBTYPE_WSTR)
            return S_FALSE;

    switch(get_dbtype_class(src_base_type))
    {
    case DBTYPE_EMPTY:
        if(common_class(dst_class)) return S_OK;
        switch(dst_class)
        {
        case DBTYPE_DATE:
        case DBTYPE_GUID:
            return S_OK;
        default:
            if(dst_base_type == DBTYPE_DBTIMESTAMP) return S_OK;
            return S_FALSE;
        }

    case DBTYPE_NULL:
        switch(dst_base_type)
        {
        case DBTYPE_NULL:
        case DBTYPE_VARIANT: return S_OK;
        default: return S_FALSE;
        }

    case DBTYPE_I4:
        if(dst_base_type == DBTYPE_BYTES) return S_OK;
        /* fall through */
    case DBTYPE_I2:
        if(dst_base_type == DBTYPE_DATE) return S_OK;
        /* fall through */
    case DBTYPE_DECIMAL:
        if(common_class(dst_class)) return S_OK;
        if(dst_class == DBTYPE_DBDATE) return S_OK;
        return S_FALSE;

    case DBTYPE_BOOL:
        if(dst_base_type == DBTYPE_DATE) return S_OK;
    case DBTYPE_NUMERIC:
    case DBTYPE_CY:
        if(common_class(dst_class)) return S_OK;
        return S_FALSE;

    case DBTYPE_I8:
        if(common_class(dst_class)) return S_OK;
        if(dst_base_type == DBTYPE_BYTES) return S_OK;
        return S_FALSE;

    case DBTYPE_DATE:
        switch(dst_class)
        {
        case DBTYPE_EMPTY:
        case DBTYPE_NULL:
        case DBTYPE_I2:
        case DBTYPE_I4:
        case DBTYPE_BSTR:
        case DBTYPE_BOOL:
        case DBTYPE_VARIANT:
        case DBTYPE_I8:
        case DBTYPE_DATE:
        case DBTYPE_DBDATE:
            return S_OK;
        default: return S_FALSE;
        }

    case DBTYPE_IDISPATCH:
    case DBTYPE_VARIANT:
        switch(dst_base_type)
        {
        case DBTYPE_IDISPATCH:
        case DBTYPE_ERROR:
        case DBTYPE_IUNKNOWN:
            return S_OK;
        }
        /* fall through */
    case DBTYPE_BSTR:
        if(common_class(dst_class)) return S_OK;
        switch(dst_class)
        {
        case DBTYPE_DATE:
        case DBTYPE_GUID:
        case DBTYPE_BYTES:
        case DBTYPE_DBDATE:
            return S_OK;
        default: return S_FALSE;
        }

    case DBTYPE_ERROR:
        switch(dst_base_type)
        {
        case DBTYPE_BSTR:
        case DBTYPE_ERROR:
        case DBTYPE_VARIANT:
        case DBTYPE_WSTR:
            return S_OK;
        default: return S_FALSE;
        }

    case DBTYPE_IUNKNOWN:
        switch(dst_base_type)
        {
        case DBTYPE_EMPTY:
        case DBTYPE_NULL:
        case DBTYPE_IDISPATCH:
        case DBTYPE_VARIANT:
        case DBTYPE_IUNKNOWN:
            return S_OK;
        default: return S_FALSE;
        }

    case DBTYPE_BYTES:
        if(dst_class == DBTYPE_I4 || dst_class == DBTYPE_I8) return S_OK;
        /* fall through */
    case DBTYPE_GUID:
        switch(dst_class)
        {
        case DBTYPE_EMPTY:
        case DBTYPE_NULL:
        case DBTYPE_BSTR:
        case DBTYPE_VARIANT:
        case DBTYPE_GUID:
        case DBTYPE_BYTES:
            return S_OK;
        default: return S_FALSE;
        }

    case DBTYPE_DBDATE:
        switch(dst_class)
        {
        case DBTYPE_EMPTY:
        case DBTYPE_NULL:
        case DBTYPE_DATE:
        case DBTYPE_BSTR:
        case DBTYPE_VARIANT:
        case DBTYPE_DBDATE:
            return S_OK;
        default: return S_FALSE;
        }

    }
    return S_FALSE;
}

static HRESULT WINAPI convert_GetConversionSize(IDataConvert* iface,
                                                DBTYPE wSrcType, DBTYPE wDstType,
                                                DBLENGTH *pcbSrcLength, DBLENGTH *pcbDstLength,
                                                void *pSrc)
{
    convert *This = impl_from_IDataConvert(iface);
    FIXME("(%p)->(%d, %d, %p, %p, %p): stub\n", This, wSrcType, wDstType, pcbSrcLength, pcbDstLength, pSrc);

    return E_NOTIMPL;
}

static const struct IDataConvertVtbl convert_vtbl =
{
    convert_QueryInterface,
    convert_AddRef,
    convert_Release,
    convert_DataConvert,
    convert_CanConvert,
    convert_GetConversionSize
};

static HRESULT WINAPI dcinfo_QueryInterface(IDCInfo* iface, REFIID riid, void **obj)
{
    convert *This = impl_from_IDCInfo(iface);

    return IDataConvert_QueryInterface((IDataConvert *)This, riid, obj);
}

static ULONG WINAPI dcinfo_AddRef(IDCInfo* iface)
{
    convert *This = impl_from_IDCInfo(iface);

    return IDataConvert_AddRef((IDataConvert *)This);
}

static ULONG WINAPI dcinfo_Release(IDCInfo* iface)
{
    convert *This = impl_from_IDCInfo(iface);

    return IDataConvert_Release((IDataConvert *)This);
}

static HRESULT WINAPI dcinfo_GetInfo(IDCInfo *iface, ULONG num, DCINFOTYPE types[], DCINFO **info_ptr)
{
    convert *This = impl_from_IDCInfo(iface);
    ULONG i;
    DCINFO *infos;

    TRACE("(%p)->(%d, %p, %p)\n", This, num, types, info_ptr);

    *info_ptr = infos = CoTaskMemAlloc(num * sizeof(*infos));
    if(!infos) return E_OUTOFMEMORY;

    for(i = 0; i < num; i++)
    {
        infos[i].eInfoType = types[i];
        VariantInit(&infos[i].vData);

        switch(types[i])
        {
        case DCINFOTYPE_VERSION:
            V_VT(&infos[i].vData) = VT_UI4;
            V_UI4(&infos[i].vData) = This->version;
            break;
        }
    }

    return S_OK;
}

static HRESULT WINAPI dcinfo_SetInfo(IDCInfo* iface, ULONG num, DCINFO info[])
{
    convert *This = impl_from_IDCInfo(iface);
    ULONG i;
    HRESULT hr = S_OK;

    TRACE("(%p)->(%d, %p)\n", This, num, info);

    for(i = 0; i < num; i++)
    {
        switch(info[i].eInfoType)
        {
        case DCINFOTYPE_VERSION:
            if(V_VT(&info[i].vData) != VT_UI4)
            {
                FIXME("VERSION with vt %x\n", V_VT(&info[i].vData));
                hr = DB_S_ERRORSOCCURRED;
                break;
            }
            This->version = V_UI4(&info[i].vData);
            break;

        default:
            FIXME("Unhandled info type %d (vt %x)\n", info[i].eInfoType, V_VT(&info[i].vData));
        }
    }
    return hr;
}

static const struct IDCInfoVtbl dcinfo_vtbl =
{
    dcinfo_QueryInterface,
    dcinfo_AddRef,
    dcinfo_Release,
    dcinfo_GetInfo,
    dcinfo_SetInfo
};

HRESULT create_oledb_convert(IUnknown *outer, void **obj)
{
    convert *This;

    TRACE("(%p, %p)\n", outer, obj);

    *obj = NULL;

    if(outer) return CLASS_E_NOAGGREGATION;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    if(!This) return E_OUTOFMEMORY;

    This->lpVtbl = &convert_vtbl;
    This->lpDCInfoVtbl = &dcinfo_vtbl;
    This->ref = 1;
    This->version = 0x110;

    *obj = &This->lpVtbl;

    return S_OK;
}
