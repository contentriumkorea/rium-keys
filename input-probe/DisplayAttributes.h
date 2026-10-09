#pragma once
#include "Probe.h"
#include <new>
inline constexpr GUID InputAttributeId={0x674e2e6c,0x6c24,0x4f83,{0x91,0x07,0xbb,0x6e,0x93,0x33,0x64,0x10}};
// `objects` includes attribute objects retained by TSF after service deactivation.
class InputAttribute final:public ITfDisplayAttributeInfo{
    LONG refs=1;TF_DISPLAYATTRIBUTE attribute{};
public:
    InputAttribute(){InterlockedIncrement(&objects);Reset();}
    ~InputAttribute(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_ITfDisplayAttributeInfo)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetGUID(GUID* value)override{if(!value)return E_POINTER;*value=InputAttributeId;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDescription(BSTR* value)override{if(!value)return E_POINTER;*value=SysAllocString(L"RIUM Keys Korean composition");return *value?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE GetAttributeInfo(TF_DISPLAYATTRIBUTE* value)override{if(!value)return E_POINTER;*value=attribute;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetAttributeInfo(const TF_DISPLAYATTRIBUTE* value)override{if(!value)return E_POINTER;attribute=*value;return S_OK;}
    HRESULT STDMETHODCALLTYPE Reset()override{attribute={};attribute.crText.type=TF_CT_SYSCOLOR;attribute.crText.nIndex=COLOR_WINDOWTEXT;attribute.crBk.type=TF_CT_NONE;attribute.lsStyle=TF_LS_SOLID;attribute.crLine.type=TF_CT_NONE;attribute.bAttr=TF_ATTR_INPUT;return S_OK;}
};
class AttributeEnumerator final:public IEnumTfDisplayAttributeInfo{
    LONG refs=1;bool consumed=false;
public:
    explicit AttributeEnumerator(bool value=false):consumed(value){InterlockedIncrement(&objects);}
    ~AttributeEnumerator(){InterlockedDecrement(&objects);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IEnumTfDisplayAttributeInfo)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumTfDisplayAttributeInfo** out)override{if(!out)return E_POINTER;*out=new(std::nothrow)AttributeEnumerator(consumed);return *out?S_OK:E_OUTOFMEMORY;}
    HRESULT STDMETHODCALLTYPE Next(ULONG count,ITfDisplayAttributeInfo** out,ULONG* fetched)override{if(fetched)*fetched=0;if(!out||(count!=1&&!fetched))return E_POINTER;if(!count)return S_OK;out[0]=nullptr;if(consumed)return S_FALSE;out[0]=new(std::nothrow)InputAttribute;if(!out[0])return E_OUTOFMEMORY;consumed=true;if(fetched)*fetched=1;return count==1?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Reset()override{consumed=false;return S_OK;}
    HRESULT STDMETHODCALLTYPE Skip(ULONG count)override{if(!count)return S_OK;if(consumed)return S_FALSE;consumed=true;return count==1?S_OK:S_FALSE;}
};
