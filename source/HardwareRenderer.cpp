#include "pch.h"
#include "HardwareRenderer.h"
#include "Mesh.h"
#include <thread>
#include <vector>

namespace dae {

	HardwareRenderer::HardwareRenderer(SDL_Window* pWindow) :
		m_pWindow(pWindow)
	{
		//Initialize
		SDL_GetWindowSize(pWindow, &m_Width, &m_Height);

		//Initialize DirectX pipeline
		const HRESULT result = InitializeDirectX();

		if (result == S_OK) m_IsInitialized = true;
		else std::cout << "DirectX initialization failed!\n";

		// Load the initial sample state
		std::vector<Mesh*> tempMeshes{};
		LoadSampleState(D3D11_FILTER_MIN_MAG_MIP_POINT, tempMeshes);

	}

	HardwareRenderer::~HardwareRenderer()
	{
		if (m_pSampleState) m_pSampleState->Release();
		if (m_pRenderTargetView) m_pRenderTargetView->Release();
		if (m_pRenderTargetBuffer) m_pRenderTargetBuffer->Release();
		if (m_pDepthStencilView) m_pDepthStencilView->Release();
		if (m_pDepthStencilBuffer) m_pDepthStencilBuffer->Release();
		if (m_pSwapChain) m_pSwapChain->Release();
		if (m_pDeviceContext)
		{
			m_pDeviceContext->ClearState();
			m_pDeviceContext->Flush();
			m_pDeviceContext->Release();
		}
		if (m_pDevice) m_pDevice->Release();
	}

	void HardwareRenderer::Render(const std::vector<Mesh*>& pMeshes, bool useUniformBackground) const
	{
		if (!m_IsInitialized)
			return;

		// Clear RTV and DSV
		ColorRGB clearColor{ useUniformBackground ? ColorRGB{ 0.1f, 0.1f, 0.1f } : ColorRGB{ 0.39f, 0.59f, 0.93f } };
		m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, &clearColor.r);
		m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Set pipeline + Invoke drawcalls (= render)
		for (Mesh* pMesh : pMeshes)
		{
			pMesh->HardwareRender(m_pDeviceContext);
		}

		// Present backbuffer (swap)
		m_pSwapChain->Present(0, 0);
	}

	void HardwareRenderer::ToggleSampleState(const std::vector<Mesh*>& pMeshes)
	{
		// Go to the next sample state
		m_SampleState = static_cast<SampleState>((static_cast<int>(m_SampleState) + 1) % (static_cast<int>(SampleState::Anisotropic) + 1));
		HANDLE m_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

		// Get the right D3D11 Filter
		D3D11_FILTER newFilter{};
		SetConsoleTextAttribute(m_hConsole, 10); // 10 is the color code for green
		std::cout << "**(HARDWARE) Sampler Filter = ";
		switch (m_SampleState)
		{
		case SampleState::Point:
			std::cout << "POINT\n";
			newFilter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			break;
		case  SampleState::Linear:
			std::cout << "LINEAR\n";
			newFilter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			break;
		case  SampleState::Anisotropic:
			std::cout << "ANISOTROPIC\n";
			newFilter = D3D11_FILTER_ANISOTROPIC;
			break;
		}

		// Load the new filter in the sample state
		LoadSampleState(newFilter, pMeshes);
	}

	void HardwareRenderer::SetCulling(CullMode mode, std::vector<Mesh*>& meshes)
	{
		D3D11_RASTERIZER_DESC rasterizer = {};
		rasterizer.FillMode = D3D11_FILL_SOLID;
		rasterizer.CullMode = (mode == CullMode::Back) ? D3D11_CULL_BACK :
			(mode == CullMode::Front) ? D3D11_CULL_FRONT : D3D11_CULL_NONE;
		rasterizer.FrontCounterClockwise = false;
		rasterizer.DepthBias = 0;
		rasterizer.SlopeScaledDepthBias = 0.0f;
		rasterizer.DepthBiasClamp = 0.0f;
		rasterizer.DepthClipEnable = true;
		rasterizer.ScissorEnable = false;
		rasterizer.MultisampleEnable = false;
		rasterizer.AntialiasedLineEnable = false;

		ID3D11RasterizerState* newState;
		m_pDevice->CreateRasterizerState(&rasterizer, &newState);

		if (m_pRasterizerState) m_pRasterizerState->Release();
		m_pRasterizerState = newState;

		for (auto mesh : meshes)
			mesh->SetRasterizerState(m_pRasterizerState);
	}

	HRESULT HardwareRenderer::InitializeDirectX()
	{
		// Create Device and DeviceContext
		HRESULT result = CreateDevice();
		if (FAILED(result)) return result;

		// Create Swapchain
		result = CreateSwapChain();
		if (FAILED(result)) return result;

		// Create DepthStencil (DS) and DepthStencilView (DSV)
		result = CreateDepthStencil();
		if (FAILED(result)) return result;

		// Create RenderTargetView (RTV)
		result = CreateRenderTargetView();
		if (FAILED(result)) return result;

		// Set RTV, DSV, and viewport
		m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);
		return S_OK;
	}

	void HardwareRenderer::LoadSampleState(D3D11_FILTER filter, const std::vector<Mesh*>& pMeshes)
	{
		// Create the SampleState description
		D3D11_SAMPLER_DESC sampleDesc{};
		sampleDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampleDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampleDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

		sampleDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

		sampleDesc.MipLODBias = 0.0f;

		sampleDesc.MinLOD = -D3D11_FLOAT32_MAX;
		sampleDesc.MaxLOD = D3D11_FLOAT32_MAX;

		sampleDesc.MaxAnisotropy = 1;

		sampleDesc.Filter = filter;

		// Release the current sample state if one exists
		if (m_pSampleState) m_pSampleState->Release();

		// Create a new sample state
		HRESULT hr = m_pDevice->CreateSamplerState(&sampleDesc, &m_pSampleState);
		if (FAILED(hr))
		{
			std::wcout << L"m_pSampleState failed to load\n";
			return;
		}

		// Update the sample state in all the meshes
		for (Mesh* pMesh : pMeshes)
		{
			pMesh->SetSamplerState(m_pSampleState);
		}
	}

	//initialize dirextX

	HRESULT HardwareRenderer::CreateDevice()
	{
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
		uint32_t createDeviceFlags = 0;

		return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, createDeviceFlags, &featureLevel, 1, D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pDeviceContext);
	}

	HRESULT HardwareRenderer::CreateSwapChain()
	{
		IDXGIFactory1* pDxgiFactory{};
		HRESULT result = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pDxgiFactory));
		if (FAILED(result)) return result;

		// Create the swapchain description
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};

		swapChainDesc.BufferDesc.Width = m_Width;
		swapChainDesc.BufferDesc.Height = m_Height;

		swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;

		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;

		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

		swapChainDesc.BufferCount = 1;

		swapChainDesc.Windowed = true;

		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		swapChainDesc.Flags = 0;

		// Get the handle (HWND) from the SDL backbuffer
		SDL_SysWMinfo sysWMInfo{};
		SDL_VERSION(&sysWMInfo.version);
		SDL_GetWindowWMInfo(m_pWindow, &sysWMInfo);
		swapChainDesc.OutputWindow = sysWMInfo.info.win.window;

		// Create swapchain
		result = pDxgiFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
		pDxgiFactory->Release();

		return result;
	}

	HRESULT HardwareRenderer::CreateDepthStencil()
	{
		// Resource
		D3D11_TEXTURE2D_DESC depthStencilDesc{};
		depthStencilDesc.Width = m_Width;
		depthStencilDesc.Height = m_Height;

		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.ArraySize = 1;

		depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;

		depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		depthStencilDesc.CPUAccessFlags = 0;

		depthStencilDesc.MiscFlags = 0;

		// View
		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = depthStencilDesc.Format;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		// Create the depth stencil buffer and view
		HRESULT hr = m_pDevice->CreateTexture2D(&depthStencilDesc, nullptr, &m_pDepthStencilBuffer);
		if (FAILED(hr)) return hr;
		hr = m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer, &depthStencilViewDesc, &m_pDepthStencilView);
		if (FAILED(hr)) return hr;

		// Bind the render target view and depth stencil view to the pipeline
		m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);

		// Create the viewport
		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(m_Width);
		viewport.Height = static_cast<float>(m_Height);

		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;

		// Set the viewport
		m_pDeviceContext->RSSetViewports(1, &viewport);

		return S_OK;
	}

	HRESULT HardwareRenderer::CreateRenderTargetView()
	{
		// Get the pointer to the back buffer
		ID3D11Texture2D* pBackBuffer{};
		HRESULT hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
		if (FAILED(hr)) return hr;

		// Create the render target view
		hr = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
		pBackBuffer->Release();
		if (FAILED(hr)) return hr;

		return S_OK;
	}

	//getters
	ID3D11Device* HardwareRenderer::GetDevice() const
	{
		return m_pDevice;
	}

	ID3D11SamplerState* HardwareRenderer::GetSampleState() const
	{
		return m_pSampleState;
	}

}

