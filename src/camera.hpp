#pragma once

#define WIN32_LEAN_AND_MEAN

#include "qedit.h"
#include "dshow.h"
#include <atlbase.h>
#include <windows.h>
#include <opencv2/opencv.hpp>
#pragma comment(lib,"strmiids.lib") 

#define MYFREEMEDIATYPE(mt)	{if ((mt).cbFormat != 0)		\
					{CoTaskMemFree((PVOID)(mt).pbFormat);	\
					(mt).cbFormat = 0;						\
					(mt).pbFormat = NULL;					\
				}											\
				if ((mt).pUnk != NULL)						\
				{											\
					(mt).pUnk->Release();					\
					(mt).pUnk = NULL;						\
				}}									

class Camera  
{
private:

	bool m_bConnected, m_bLock, m_bChanged;
	int m_nWidth, m_nHeight;
	long m_nBufferSize;

	cv::Mat m_frame;

	CComPtr<IGraphBuilder> m_pGraph;
	CComPtr<ISampleGrabber> m_pSampleGrabber;
	CComPtr<IMediaControl> m_pMediaControl;
	CComPtr<IMediaEvent> m_pMediaEvent;

	CComPtr<IBaseFilter> m_pSampleGrabberFilter;
	CComPtr<IBaseFilter> m_pDeviceFilter;
	CComPtr<IBaseFilter> m_pNullFilter;

	CComPtr<IPin> m_pGrabberInput;
	CComPtr<IPin> m_pGrabberOutput;
	CComPtr<IPin> m_pCameraOutput;
	CComPtr<IPin> m_pNullInputPin;

private:

	bool BindFilter(int nCamID, IBaseFilter **pFilter)
	{
		if (nCamID < 0)
		{
			return false;
		}

		// enumerate all video capture devices
		CComPtr<ICreateDevEnum> pCreateDevEnum;
		HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pCreateDevEnum);
		if (hr != NOERROR)
		{
			return false;
		}

		CComPtr<IEnumMoniker> pEm;
		hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEm, 0);
		if (hr != NOERROR)
		{
			return false;
		}

		pEm->Reset();
		ULONG cFetched;
		IMoniker *pM;
		int index = 0;
		while (hr = pEm->Next(1, &pM, &cFetched), hr == S_OK, index <= nCamID)
		{
			IPropertyBag *pBag;
			hr = pM->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pBag);
			if (SUCCEEDED(hr))
			{
				VARIANT var;
				var.vt = VT_BSTR;
				hr = pBag->Read(L"FriendlyName", &var, NULL);
				if (hr == NOERROR)
				{
					if (index == nCamID)
					{
						pM->BindToObject(0, 0, IID_IBaseFilter, (void**)pFilter);
					}
					SysFreeString(var.bstrVal);
				}
				pBag->Release();
			}
			pM->Release();
			index++;
		}

		pCreateDevEnum = NULL;
		return true;
	}

	// -----------------------------------------------------------------------
	// 将输入crossbar变成PhysConn_Video_Composite

	void SetCrossBar()
	{
		int i;
		IAMCrossbar *pXBar1 = NULL;
		ICaptureGraphBuilder2 *pBuilder = NULL;

		HRESULT hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void **)&pBuilder);

		if (SUCCEEDED(hr))
		{
			hr = pBuilder->SetFiltergraph(m_pGraph);
		}

		hr = pBuilder->FindInterface(&LOOK_UPSTREAM_ONLY, NULL, m_pDeviceFilter, IID_IAMCrossbar, (void**)&pXBar1);

		if (SUCCEEDED(hr))
		{
			long OutputPinCount, InputPinCount;
			long PinIndexRelated, PhysicalType;
			long inPort = 0, outPort = 0;

			pXBar1->get_PinCounts(&OutputPinCount, &InputPinCount);
			for (i = 0; i < InputPinCount; i++)
			{
				pXBar1->get_CrossbarPinInfo(TRUE, i, &PinIndexRelated, &PhysicalType);
				if (PhysConn_Video_Composite == PhysicalType)
				{
					inPort = i;
					break;
				}
			}
			for (i = 0; i < OutputPinCount; i++)
			{
				pXBar1->get_CrossbarPinInfo(FALSE, i, &PinIndexRelated, &PhysicalType);
				if (PhysConn_Video_VideoDecoder == PhysicalType)
				{
					outPort = i;
					break;
				}
			}

			if (S_OK == pXBar1->CanRoute(outPort, inPort))
			{
				pXBar1->Route(outPort, inPort);
			}
			pXBar1->Release();
		}
		pBuilder->Release();
	}

public:

	// -----------------------------------------------------------------------

	Camera()
	{
		m_bConnected = m_bLock = m_bChanged = false;
		m_nWidth = m_nHeight = 0;
		m_nBufferSize = 0;

		m_frame = NULL;

		m_pNullFilter = NULL;
		m_pMediaEvent = NULL;
		m_pSampleGrabberFilter = NULL;
		m_pGraph = NULL;

		CoInitialize(NULL);
	}

	// -----------------------------------------------------------------------

	virtual ~Camera()
	{
		CloseCamera();
		CoUninitialize();
	}

	// -----------------------------------------------------------------------
	// 打开摄像头，nCamID指定打开哪个摄像头，取值可以为0,1,2,...
	// bDisplayProperties指示是否自动弹出摄像头属性页
	// nWidth和nHeight设置的摄像头的宽和高，
	// 如果摄像头不支持所设定的宽度和高度，则返回false
	bool OpenCamera(int nCamID, bool bDisplayProperties = true, 
		int nWidth = 320, int nHeight = 240)
	{
		HRESULT hr = S_OK;

		CoInitialize(NULL);
		// Create the Filter Graph Manager.
		hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC, IID_IGraphBuilder, (void **)&m_pGraph);

		hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (LPVOID *)&m_pSampleGrabberFilter);

		hr = m_pGraph->QueryInterface(IID_IMediaControl, (void **)&m_pMediaControl);
		hr = m_pGraph->QueryInterface(IID_IMediaEvent, (void **)&m_pMediaEvent);

		hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (LPVOID*)&m_pNullFilter);

		hr = m_pGraph->AddFilter(m_pNullFilter, L"NullRenderer");

		hr = m_pSampleGrabberFilter->QueryInterface(IID_ISampleGrabber, (void**)&m_pSampleGrabber);

		AM_MEDIA_TYPE   mt;
		ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE));
		mt.majortype = MEDIATYPE_Video;
		mt.subtype = MEDIASUBTYPE_RGB24;
		mt.formattype = FORMAT_VideoInfo;
		hr = m_pSampleGrabber->SetMediaType(&mt);
		MYFREEMEDIATYPE(mt);

		m_pGraph->AddFilter(m_pSampleGrabberFilter, L"Grabber");

		// Bind Device Filter.  We know the device because the id was passed in
		BindFilter(nCamID, &m_pDeviceFilter);
		m_pGraph->AddFilter(m_pDeviceFilter, NULL);

		CComPtr<IEnumPins> pEnum;
		m_pDeviceFilter->EnumPins(&pEnum);

		hr = pEnum->Reset();
		hr = pEnum->Next(1, &m_pCameraOutput, NULL);

		pEnum = NULL;
		m_pSampleGrabberFilter->EnumPins(&pEnum);
		pEnum->Reset();
		hr = pEnum->Next(1, &m_pGrabberInput, NULL);

		pEnum = NULL;
		m_pSampleGrabberFilter->EnumPins(&pEnum);
		pEnum->Reset();
		pEnum->Skip(1);
		hr = pEnum->Next(1, &m_pGrabberOutput, NULL);

		pEnum = NULL;
		m_pNullFilter->EnumPins(&pEnum);
		pEnum->Reset();
		hr = pEnum->Next(1, &m_pNullInputPin, NULL);

		//SetCrossBar();

		if (bDisplayProperties)
		{
			CComPtr<ISpecifyPropertyPages> pPages;

			HRESULT hr = m_pCameraOutput->QueryInterface(IID_ISpecifyPropertyPages, (void**)&pPages);
			if (SUCCEEDED(hr))
			{
				PIN_INFO PinInfo;
				m_pCameraOutput->QueryPinInfo(&PinInfo);

				CAUUID caGUID;
				pPages->GetPages(&caGUID);

				OleCreatePropertyFrame(NULL, 0, 0,
					L"Property Sheet", 1,
					(IUnknown **)&(m_pCameraOutput.p),
					caGUID.cElems, caGUID.pElems,
					0, 0, NULL);

				CoTaskMemFree(caGUID.pElems);
				PinInfo.pFilter->Release();
			}
			pPages = NULL;
		}
		else
		{
			IAMStreamConfig *iconfig = NULL;
			hr = m_pCameraOutput->QueryInterface(IID_IAMStreamConfig, (void**)&iconfig);

			AM_MEDIA_TYPE *pmt;
			if (iconfig->GetFormat(&pmt) != S_OK)
			{
				//printf("GetFormat Failed ! \n");
				return false;
			}

			// 3、考虑如果此时的的图像大小正好是 nWidth * nHeight，则就不用修改了。
			if ((pmt->lSampleSize != (nWidth * nHeight * 3)) && (pmt->formattype == FORMAT_VideoInfo))
			{
				VIDEOINFOHEADER *phead = (VIDEOINFOHEADER*)(pmt->pbFormat);
				phead->bmiHeader.biWidth = nWidth;
				phead->bmiHeader.biHeight = nHeight;
				if ((hr = iconfig->SetFormat(pmt)) != S_OK)
				{
					return false;
				}
			}

			iconfig->Release();
			iconfig = NULL;
			MYFREEMEDIATYPE(*pmt);
		}

		hr = m_pGraph->Connect(m_pCameraOutput, m_pGrabberInput);
		hr = m_pGraph->Connect(m_pGrabberOutput, m_pNullInputPin);

		if (FAILED(hr))
		{
			switch (hr)
			{
			case VFW_S_NOPREVIEWPIN:
				break;
			case E_FAIL:
				break;
			case E_INVALIDARG:
				break;
			case E_POINTER:
				break;
			}
		}

		m_pSampleGrabber->SetBufferSamples(TRUE);
		m_pSampleGrabber->SetOneShot(TRUE);

		hr = m_pSampleGrabber->GetConnectedMediaType(&mt);
		if (FAILED(hr))
		{
			return false;
		}

		VIDEOINFOHEADER *videoHeader;
		videoHeader = reinterpret_cast<VIDEOINFOHEADER*>(mt.pbFormat);
		m_nWidth = videoHeader->bmiHeader.biWidth;
		m_nHeight = videoHeader->bmiHeader.biHeight;
		m_bConnected = true;

		pEnum = NULL;
		return true;
	}

	// -----------------------------------------------------------------------

	void CloseCamera()
	{
		if (m_bConnected)
		{
			m_pMediaControl->Stop();
		}

		m_pGraph = NULL;
		m_pDeviceFilter = NULL;
		m_pMediaControl = NULL;
		m_pSampleGrabberFilter = NULL;
		m_pSampleGrabber = NULL;
		m_pGrabberInput = NULL;
		m_pGrabberOutput = NULL;
		m_pCameraOutput = NULL;
		m_pMediaEvent = NULL;
		m_pNullFilter = NULL;
		m_pNullInputPin = NULL;
		m_bConnected = m_bLock = m_bChanged = false;
		m_nWidth = m_nHeight = 0;
		m_nBufferSize = 0;
	}

	// -----------------------------------------------------------------------
	// 返回摄像头的数目
	// 可以不用创建CCameraDS实例，采用int c=CCameraDS::CameraCount();得到结果。
	static int CameraCount()
	{
		int count = 0;
		CoInitialize(NULL);

		// enumerate all video capture devices
		CComPtr<ICreateDevEnum> pCreateDevEnum;
		HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pCreateDevEnum);

		CComPtr<IEnumMoniker> pEm;
		hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEm, 0);
		if (hr != NOERROR)
		{
			return count;
		}

		pEm->Reset();
		ULONG cFetched;
		IMoniker *pM;
		while (hr = pEm->Next(1, &pM, &cFetched), hr == S_OK)
		{
			count++;
		}

		pCreateDevEnum = NULL;
		pEm = NULL;
		return count;
	}

	// -----------------------------------------------------------------------
	// 根据摄像头的编号返回摄像头的名字
	// nCamID: 摄像头编号
	// sName: 用于存放摄像头名字的数组
	// nBufferSize: sName的大小
	// 可以不用创建CCameraDS实例，采用CCameraDS::CameraName();得到结果。
	static int CameraName(int nCamID, char* sName, int nBufferSize)
	{
		int count = 0;
		CoInitialize(NULL);

		// enumerate all video capture devices
		CComPtr<ICreateDevEnum> pCreateDevEnum;
		HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pCreateDevEnum);

		CComPtr<IEnumMoniker> pEm;
		hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEm, 0);
		if (hr != NOERROR) return 0;

		pEm->Reset();
		ULONG cFetched;
		IMoniker *pM;
		while (hr = pEm->Next(1, &pM, &cFetched), hr == S_OK)
		{
			if (count == nCamID)
			{
				IPropertyBag *pBag = 0;
				hr = pM->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pBag);
				if (SUCCEEDED(hr))
				{
					VARIANT var;
					var.vt = VT_BSTR;
					hr = pBag->Read(L"FriendlyName", &var, NULL); //还有其他属性,像描述信息等等...
					if (hr == NOERROR)
					{
						//获取设备名称			
						WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, sName, nBufferSize, "", NULL);

						SysFreeString(var.bstrVal);
					}
					pBag->Release();
				}
				pM->Release();

				break;
			}
			count++;
		}

		pCreateDevEnum = NULL;
		pEm = NULL;

		return 1;
	}

	// -----------------------------------------------------------------------

	cv::Mat QueryFrame()
	{
		if (m_pMediaControl->Run() == S_FALSE)
			return cv::Mat();

		long evCode, size = 0;
		m_pMediaEvent->WaitForCompletion(INFINITE, &evCode);
		m_pSampleGrabber->GetCurrentBuffer(&size, NULL);

		//if the buffer size changed
		if (size != m_nBufferSize)
		{
			m_nBufferSize = size;
			m_frame = cv::Mat(m_nHeight, m_nWidth, CV_8UC3);
		}

		m_pSampleGrabber->GetCurrentBuffer(&m_nBufferSize, (long*)m_frame.data);
		return m_frame;
	}
};
