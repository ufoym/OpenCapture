#include <new>
#include <windows.h>
#include <windowsx.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfcaptureengine.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <commctrl.h>
#include <d3d11.h>
#include <iostream>


struct DeviceParam
{
	DeviceParam() : pAttributes(NULL), ppDevices(NULL), count(0) {}
	~DeviceParam()
	{
		for (DWORD i = 0; i < count; i++)
			release(&ppDevices[i]);
		CoTaskMemFree(ppDevices);
		release(&pAttributes);
	}
	template <class T> inline void release(T **ppT)
	{
		if (*ppT) {
			(*ppT)->Release();
			*ppT = NULL;
		}
	}
	IMFAttributes *pAttributes;
	IMFActivate **ppDevices;
	UINT32      count;
	UINT32      selection;
};

int main()
{
	DeviceParam param;

	HRESULT hr = MFCreateAttributes(&param.pAttributes, 1);
	if (FAILED(hr))
		return -1;
	hr = param.pAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	if (FAILED(hr))
		return -1;

	// Enumerate devices.
	hr = MFEnumDeviceSources(param.pAttributes, &param.ppDevices, &param.count);
	if (FAILED(hr))
		return -1;

	for (DWORD i = 0; i < param.count; i++)
	{
		WCHAR *szFriendlyName = NULL;
		UINT32 cchName;

		hr = param.ppDevices[i]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&szFriendlyName, &cchName);
		if (FAILED(hr))
			break;

		std::wcout << szFriendlyName << std::endl;
		CoTaskMemFree(szFriendlyName);
	}

	return 0;
}