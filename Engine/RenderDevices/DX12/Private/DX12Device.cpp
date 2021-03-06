#include "..\Public\DX12Device.h"
#include "..\..\..\Frame\Public\RenderTarget.h"
#include "..\..\..\Platforms\Windows\Public\EngineWindows.h"
#include "..\..\..\Platforms\Windows\Public\RenderWindowWindows.h"
#include "..\..\..\Platforms\Windows\Public\WindowsUtility.h"
#include "..\Public\DDSTextureLoader12.h"
#include "..\..\..\Utility\Public\MeshUtility.h"
#include "..\..\..\Frame\Public\Mesh.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")


struct VertexDataInfo
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 TexCoord;

	VertexDataInfo() {}

	VertexDataInfo(
		float px, float py, float pz,
		float nx, float ny, float nz,
		float u, float v) :
		Pos(px, py, pz),
		Normal(nx, ny, nz),
		TexCoord(u, v) {}

};

int DX12RenderCommandList::Flush()
{
	return 0;
}

int DX12RenderCommandList::reset()
{
	return 0;
}

DX12Device::DX12Device()
{

}

DX12Device::~DX12Device()
{
	UnInit();
}

int DX12Device::Init()
{
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable DX12 debug layer
	{
		Microsoft::WRL::ComPtr <ID3D12Debug> debugController;
		D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
		debugController->EnableDebugLayer();
	}
#endif

	mSwapChainBufferCount = 2;
	mChainBufferndex = 0;
	mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	CreateDevice();
	CreateCommandObjects();
	CreateSwapChain();

	// Create swipe chain
	IRenderTarget* renderTarget = EngineWindows::GetInstance()->GetRenderTarget();
	assert(renderTarget != nullptr);

	// viewport
	mViewPort = { 0.0f, 0.0f, static_cast<float>(renderTarget->GetWidth()), static_cast<float>(renderTarget->GetHeight()), 0.0f, 1.0f };

	// Scissor Rectangle
	mScissorRect = { 0, 0, renderTarget->GetWidth(), renderTarget->GetHeight() };

	// Crete fence
	THROW_IF_FAILED(mDX12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mDX12Fence)));
	mFenceValue = 1;

	// Create event
	mEventHandle = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);

	FlushCommandQueue();

	THROW_IF_FAILED(mDX12CommandAllocator->Reset());
	// Reset command list
	THROW_IF_FAILED(mDX12CommandList->Reset(mDX12CommandAllocator.Get(), mIDX12PipleLineState.Get()));


	// Create render view description
	D3D12_DESCRIPTOR_HEAP_DESC rtDescriptorHeapDesc;
	rtDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtDescriptorHeapDesc.NumDescriptors = mSwapChainBufferCount;
	rtDescriptorHeapDesc.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&rtDescriptorHeapDesc, IID_PPV_ARGS(&mDX12DescriptorHeapRenderTarget)));

	D3D12_DESCRIPTOR_HEAP_DESC dsDescriptorHeapDesc;
	dsDescriptorHeapDesc.NumDescriptors = 1;
	dsDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsDescriptorHeapDesc.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&dsDescriptorHeapDesc, IID_PPV_ARGS(&mDX12DescriptorHeapDepthStencil)));

	mRTVDescriptorSize = mDX12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDSVDescriptorSize = mDX12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCBVSRVDescriptorSize = mDX12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Create render view
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(mDX12DescriptorHeapRenderTarget->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < mSwapChainBufferCount; i++)
	{
		mDXGISwapChain->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i]));
		mDX12Device->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, descriptorHandle);
		descriptorHandle.Offset(1, mRTVDescriptorSize);
	}
	mChainBufferndex = 0;

	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = renderTarget->GetWidth();
	depthStencilDesc.Height = renderTarget->GetHeight();
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = mDepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue;
	clearValue.Format = mDepthStencilFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;
	THROW_IF_FAILED(mDX12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&clearValue,
		IID_PPV_ARGS(&mDepthStencilBuffer)));

	CreateTexture();

	CreateShader();

	CreateGeometry();

	// Create root signature
	CreateRootSignature();

	// Create graphic pipeline state 
	CreatePipelineState();

	THROW_IF_FAILED(mDX12CommandList->Close());

	// Execute command list
	ID3D12CommandList* CommandLists[] = { mDX12CommandList.Get() };
	mDX12CommandQueue->ExecuteCommandLists(1, CommandLists);

	FlushCommandQueue();


	mDX12CommandAllocator->Reset();

	// Reset command list
	THROW_IF_FAILED(mDX12CommandList->Reset(mDX12CommandAllocator.Get(), mIDX12PipleLineState.Get()));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	mDX12Device->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, GetDepthStencilView());

	mDX12CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	THROW_IF_FAILED(mDX12CommandList->Close());

	// Execute command list
	mDX12CommandQueue->ExecuteCommandLists(1, CommandLists);

	FlushCommandQueue();

	return 0;
}

int DX12Device::UnInit()
{
	if (mDX12Device != nullptr)
	{
		FlushCommandQueue();
	}

	return 0;
}

int DX12Device::Clear()
{
	mDX12CommandAllocator->Reset();

	// Reset command list
	THROW_IF_FAILED(mDX12CommandList->Reset(mDX12CommandAllocator.Get(), mIDX12PipleLineState.Get()));

	mDX12CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear color and depth stencil
	float ClearColor[] = { 0.690196097f, 0.768627524f, 0.870588303f, 1.000000000f };
	mDX12CommandList->ClearRenderTargetView(GetBackBufferView(), ClearColor, 0, nullptr);
	mDX12CommandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	return 0;
}

IRenderTarget*  DX12Device::CreateRenderTarget()
{
	// Create render view description
	Microsoft::WRL::ComPtr <ID3D12DescriptorHeap> descriptorHeapRenderTarget;
	D3D12_DESCRIPTOR_HEAP_DESC rtDescriptorHeapDesc;
	rtDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtDescriptorHeapDesc.NumDescriptors = 1;
	rtDescriptorHeapDesc.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&rtDescriptorHeapDesc, IID_PPV_ARGS(&descriptorHeapRenderTarget)));

	Microsoft::WRL::ComPtr <ID3D12Resource> renderTarget;
	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(descriptorHeapRenderTarget->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < mSwapChainBufferCount; i++)
	{
		mDXGISwapChain->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i]));
		mDX12Device->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, descriptorHandle);
		descriptorHandle.Offset(1, mRTVDescriptorSize);
	}

	return 0;
}

int DX12Device::SetRenderTarget(IRenderTarget* rt)
{
	mDX12CommandList->OMSetRenderTargets(1, &GetBackBufferView(), true, &GetDepthStencilView());

	return 0;
}

int DX12Device::SetViewPort()
{
	// Set view port
	mDX12CommandList->RSSetViewports(1, &mViewPort);
	return 0;
}

int DX12Device::Draw()
{
	// Set scissor
	mDX12CommandList->RSSetScissorRects(1, &mScissorRect);

	// Set render target
	mDX12CommandList->OMSetRenderTargets(1, &GetBackBufferView(), true, &GetDepthStencilView());

	// Set root signature
	mDX12CommandList->SetGraphicsRootSignature(mDX12RootSignature.Get());

	mDX12CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mDX12CommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
	mDX12CommandList->IASetIndexBuffer(&mIndexBufferView);
	ID3D12DescriptorHeap* descriptorHeaps[] = { mSRVDescriptorHeap.Get() };
	mDX12CommandList->SetDescriptorHeaps(1, descriptorHeaps);
	mDX12CommandList->SetGraphicsRootDescriptorTable(0, mSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	CD3DX12_GPU_DESCRIPTOR_HANDLE EnvTexDescriptor(mSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	EnvTexDescriptor.Offset(1, mCBVSRVDescriptorSize);
	mDX12CommandList->SetGraphicsRootDescriptorTable(1, EnvTexDescriptor);
	mDX12CommandList->SetGraphicsRootConstantBufferView(2, mObjectConstantBuffer->GetGPUVirtualAddress());
	mDX12CommandList->SetGraphicsRootConstantBufferView(3, mMaterialConstantBuffer->GetGPUVirtualAddress());
	mDX12CommandList->SetGraphicsRootConstantBufferView(4, mPassConstantBuffer->GetGPUVirtualAddress());

	// Draw
	mDX12CommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

	mDX12CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	THROW_IF_FAILED(mDX12CommandList->Close());

	// Execute command list
	ID3D12CommandList* CommandLists[] = { mDX12CommandList.Get() };
	mDX12CommandQueue->ExecuteCommandLists(1, CommandLists);

	return 0;
}

int DX12Device::SetPipelineState()
{
	return 0;
}

int DX12Device::Present()
{
	// Present
	THROW_IF_FAILED(mDXGISwapChain->Present(0, 0));

	mChainBufferndex = (mChainBufferndex + 1) % mSwapChainBufferCount;

	FlushCommandQueue();

	return 0;
}

int DX12Device::CreateObjectConstants()
{
	mWorldMatrix = DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	XMMATRIX world = XMLoadFloat4x4(&mWorldMatrix);

	XMStoreFloat4x4(&mObjectConstants.World, XMMatrixTranspose(world));

	// Create object constant buffer
	THROW_IF_FAILED(mDX12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(CalcConstantBufferByteSize(sizeof(XMMATRIX))),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mObjectConstantBuffer)));

	UINT8* objectConstantBufferData;
	mObjectConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&objectConstantBufferData));
	memcpy(objectConstantBufferData, &mObjectConstants, sizeof(mObjectConstants));
	mObjectConstantBuffer->Unmap(0, nullptr);

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDescObject;
	cbvHeapDescObject.NumDescriptors = 1;
	cbvHeapDescObject.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDescObject.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDescObject.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&cbvHeapDescObject,
		IID_PPV_ARGS(&mObjectConstantBufferHeap)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDescObject;
	cbvDescObject.BufferLocation = mObjectConstantBuffer->GetGPUVirtualAddress();
	cbvDescObject.SizeInBytes = CalcConstantBufferByteSize(sizeof(ObjectConstants));

	mDX12Device->CreateConstantBufferView(
		&cbvDescObject,
		mObjectConstantBufferHeap->GetCPUDescriptorHandleForHeapStart());

	return 0;

}

int DX12Device::CreateMaterialConstants()
{
	mMaterialConstants.Metallic = 0.5f;
	mMaterialConstants.Roughness = 0.2f;
	mMaterialConstants.Specular = 0.1f;
	mMaterialConstants.EmissiveColor = XMFLOAT4(Colors::Black);


	// Create material constant buffer
	THROW_IF_FAILED(mDX12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(CalcConstantBufferByteSize(sizeof(XMMATRIX))),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mMaterialConstantBuffer)));


	UINT8* materialConstantBufferData;
	mMaterialConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&materialConstantBufferData));
	memcpy(materialConstantBufferData, &mMaterialConstants, sizeof(mMaterialConstants));
	mMaterialConstantBuffer->Unmap(0, nullptr);

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDescMaterial;
	cbvHeapDescMaterial.NumDescriptors = 1;
	cbvHeapDescMaterial.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDescMaterial.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDescMaterial.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&cbvHeapDescMaterial,
		IID_PPV_ARGS(&mMaterialConstantBufferHeap)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDescMaterial;
	cbvDescMaterial.BufferLocation = mMaterialConstantBuffer->GetGPUVirtualAddress();
	cbvDescMaterial.SizeInBytes = CalcConstantBufferByteSize(sizeof(MaterialConstants));

	mDX12Device->CreateConstantBufferView(
		&cbvDescMaterial,
		mMaterialConstantBufferHeap->GetCPUDescriptorHandleForHeapStart());

	return 0;

}

int DX12Device::CreatePassConstants()
{
	IRenderTarget* renderTarget = EngineWindows::GetInstance()->GetRenderTarget();
	assert(renderTarget != nullptr);

	IScene* CurrentScene = EngineWindows::GetInstance()->GetCurrentScene();
	ICamera* CurrentCamera = CurrentScene->GetCurrentCamera();
	DirectonalLight* MainDirectonalLight = CurrentScene->GetMainDirectonalLight();
	Eigen::Vector3f CameraPos = CurrentCamera->GetPosition();
	Eigen::Vector3f CameraUp = CurrentCamera->GetUp();
	Eigen::Vector3f CameraDirection = CurrentCamera->GetDirection();
	Eigen::Vector3f DirectonalLightDirection = MainDirectonalLight->GetDirection();
	Eigen::Vector3f DirectonalLightColor = MainDirectonalLight->GetColor();

	// Build view matrix.
	XMVECTOR pos = XMVectorSet(CameraPos.x(), CameraPos.y(), CameraPos.z(), 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(CameraUp.x(), CameraUp.y(), CameraUp.z(), 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mViewMatrix, view);

	float AspectRatio = (float)renderTarget->GetWidth() / (float)renderTarget->GetHeight();
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI, AspectRatio, 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProjMatrix, P);

	mWorldMatrix = DirectX::XMFLOAT4X4(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f);
	XMMATRIX world = XMLoadFloat4x4(&mWorldMatrix);
	XMMATRIX proj = XMLoadFloat4x4(&mProjMatrix);

	XMStoreFloat4x4(&mPassConstants.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mPassConstants.ViewProj, XMMatrixTranspose(view * proj));
	mPassConstants.CameraPos = DirectX::XMFLOAT3(CameraPos.x(), CameraPos.y(), CameraPos.z());
	mPassConstants.CameraDir = DirectX::XMFLOAT3(CameraDirection.x(), CameraDirection.y(), CameraDirection.z());
	mPassConstants.DirectionalLightDir = DirectX::XMFLOAT3(DirectonalLightDirection.x(), DirectonalLightDirection.y(), DirectonalLightDirection.z());
	mPassConstants.DirectionalLightColor = XMFLOAT4(DirectonalLightColor.x(), DirectonalLightColor.y(), DirectonalLightColor.z(), 1.0f);


	// Create pass constant buffer
	THROW_IF_FAILED(mDX12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(CalcConstantBufferByteSize(sizeof(XMMATRIX))),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mPassConstantBuffer)));


	UINT8* passConstantBufferData;
	mPassConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&passConstantBufferData));
	memcpy(passConstantBufferData, &mPassConstants, sizeof(mPassConstants));
	mPassConstantBuffer->Unmap(0, nullptr);

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDescPass;
	cbvHeapDescPass.NumDescriptors = 1;
	cbvHeapDescPass.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDescPass.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDescPass.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&cbvHeapDescPass,
		IID_PPV_ARGS(&mPassConstantBufferHeap)));

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDescPass;
	cbvDescPass.BufferLocation = mPassConstantBuffer->GetGPUVirtualAddress();
	cbvDescPass.SizeInBytes = CalcConstantBufferByteSize(sizeof(PassConstants));

	mDX12Device->CreateConstantBufferView(
		&cbvDescPass,
		mPassConstantBufferHeap->GetCPUDescriptorHandleForHeapStart());

	return 0;

}

void DX12Device::CreateDevice()
{
	// Create DXGI factory
	THROW_IF_FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&mDXGIFactory)));

	// Create DX12 device
	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != mDXGIFactory->EnumAdapters1(adapterIndex, &mAdapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc = {};
		mAdapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}
		HRESULT Result = D3D12CreateDevice(mAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDX12Device));
		if (SUCCEEDED(Result))
		{
			break;
		}
	}
}

void DX12Device::CreateCommandObjects()
{
	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	THROW_IF_FAILED(mDX12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mDX12CommandQueue)));

	// Create command allocator
	THROW_IF_FAILED(mDX12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mDX12CommandAllocator)));

	// Create command list
	THROW_IF_FAILED(mDX12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mDX12CommandAllocator.Get(), mIDX12PipleLineState.Get(), IID_PPV_ARGS(&mDX12CommandList)));

	THROW_IF_FAILED(mDX12CommandList->Close());
}

void DX12Device::CreateSwapChain()
{
	HWND WindowHandle = nullptr;
	RenderWindowWindows *RenderWindow = dynamic_cast<RenderWindowWindows*>(EngineWindows::GetInstance()->GetRenderTarget());
	if (nullptr != RenderWindow)
	{
		WindowHandle = RenderWindow->GetRenderWindowHandle();
	}
	assert(WindowHandle != nullptr);

	IRenderTarget* renderTarget = EngineWindows::GetInstance()->GetRenderTarget();
	assert(renderTarget != nullptr);

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = renderTarget->GetWidth();
	swapChainDesc.BufferDesc.Height = renderTarget->GetHeight();
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = mBackBufferFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.OutputWindow = WindowHandle;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	THROW_IF_FAILED(mDXGIFactory->CreateSwapChain(
		mDX12CommandQueue.Get(),
		&swapChainDesc,
		&mDXGISwapChain));

}

void DX12Device::CreateShader()
{
	UINT compileFlags = 0;
	std::wstring ShaderFileNameVS(L"Engine\\Shaders\\ForwardShadingVS.hlsl");
	std::wstring ShaderFileNamePS(L"Engine\\Shaders\\ForwardShadingPS.hlsl");

	THROW_IF_FAILED(D3DCompileFromFile(ShaderFileNameVS.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", compileFlags, 0, &mVertexShader, nullptr));
	THROW_IF_FAILED(D3DCompileFromFile(ShaderFileNamePS.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", compileFlags, 0, &mPixelShader, nullptr));
}

void DX12Device::CreateGeometry()
{
	ManualMesh Mesh;
	MeshUtility::CreateSphere(Mesh, 1.0f);
	const int vertexCount = 2017;
	const int indexCount = 11904;
	VertexDataInfo vertexData[vertexCount];

	for (size_t i = 0; i < vertexCount; i++)
	{
		vertexData[i] = VertexDataInfo(Mesh.GetVertexData()[i].Pos.x(), Mesh.GetVertexData()[i].Pos.y(), Mesh.GetVertexData()[i].Pos.z(),
			Mesh.GetVertexData()[i].Normal.x(), Mesh.GetVertexData()[i].Normal.y(), Mesh.GetVertexData()[i].Normal.z(),
			Mesh.GetVertexData()[i].TexCoord.x(), Mesh.GetVertexData()[i].TexCoord.y());
	}

	std::uint16_t indexData[indexCount];

	for (size_t i = 0; i < indexCount; i++)
	{
		indexData[i] = Mesh.GetIndexData()[i];
	}

	mIndexCount = indexCount;

	// Create vertex buffer
	THROW_IF_FAILED(mDX12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexCount * sizeof(VertexDataInfo)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mVertexBuffer)));

	// Create index buffer
	THROW_IF_FAILED(mDX12Device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(indexCount * sizeof(std::uint16_t)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mIndexBuffer)));


	const UINT vbByteSize = vertexCount * sizeof(VertexDataInfo);
	const UINT ibByteSize = indexCount * sizeof(std::uint16_t);

	// Copy data
	UINT8* vertexBufferData;
	mVertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&vertexBufferData));
	memcpy(vertexBufferData, vertexData, vbByteSize);
	mVertexBuffer->Unmap(0, nullptr);

	UINT8* indexBufferData;
	mIndexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&indexBufferData));
	memcpy(indexBufferData, indexData, ibByteSize);
	mIndexBuffer->Unmap(0, nullptr);

	// Vertex buffer view
	mVertexBufferView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
	mVertexBufferView.StrideInBytes = sizeof(VertexDataInfo);
	mVertexBufferView.SizeInBytes = vbByteSize;

	mIndexBufferView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
	mIndexBufferView.Format = DXGI_FORMAT_R16_UINT;
	mIndexBufferView.SizeInBytes = ibByteSize;;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDescObject;
	cbvHeapDescObject.NumDescriptors = 1;
	cbvHeapDescObject.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDescObject.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDescObject.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&cbvHeapDescObject,
		IID_PPV_ARGS(&mObjectConstantBufferHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDescMaterial;
	cbvHeapDescMaterial.NumDescriptors = 1;
	cbvHeapDescMaterial.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDescMaterial.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDescMaterial.NodeMask = 0;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&cbvHeapDescMaterial,
		IID_PPV_ARGS(&mMaterialConstantBufferHeap)));

}

void DX12Device::CreatePipelineState()
{
	// Input Layout
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };

	UINT NumElements = sizeof(inputLayout) / sizeof(inputLayout[0]);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { inputLayout, NumElements };
	psoDesc.pRootSignature = mDX12RootSignature.Get();
	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.StencilEnable = TRUE;
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mVertexShader->GetBufferPointer()),
		mVertexShader->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mPixelShader->GetBufferPointer()),
		mPixelShader->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	THROW_IF_FAILED(mDX12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mIDX12PipleLineState)));

}

void DX12Device::CreateRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE texTableEnv;
	texTableEnv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsDescriptorTable(1, &texTableEnv, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[2].InitAsConstantBufferView(0);
	slotRootParameter[3].InitAsConstantBufferView(1);
	slotRootParameter[4].InitAsConstantBufferView(2);

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(5, slotRootParameter, 1, &pointWrap,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr <ID3DBlob> signature;
	Microsoft::WRL::ComPtr <ID3DBlob> error;
	D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
	THROW_IF_FAILED(mDX12Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&mDX12RootSignature)));

}

void DX12Device::CreateTexture()
{
	std::wstring szFileName(L"Content\\Textures\\oldwood.dds");

	THROW_IF_FAILED(CreateDDSTextureFromFile12(mDX12Device.Get(),
		mDX12CommandList.Get(), szFileName.c_str(),
		mTestTexture, mTestTextureUploadHeap));

	std::wstring szFileNameEnvironment(L"Content\\Textures\\envcube.dds");

	THROW_IF_FAILED(CreateDDSTextureFromFile12(mDX12Device.Get(),
		mDX12CommandList.Get(), szFileNameEnvironment.c_str(),
		mEnvironmentTexture, mEnvironmentTextureUploadHeap));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 2;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	THROW_IF_FAILED(mDX12Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSRVDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mTestTexture->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = mTestTexture->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	mDX12Device->CreateShaderResourceView(mTestTexture.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCBVSRVDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = mEnvironmentTexture->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = mEnvironmentTexture->GetDesc().Format;
	mDX12Device->CreateShaderResourceView(mEnvironmentTexture.Get(), &srvDesc, hDescriptor);

}

ID3D12Resource* DX12Device::GetBackBuffer() const
{
	return mRenderTargets[mChainBufferndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::GetBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mDX12DescriptorHeapRenderTarget->GetCPUDescriptorHandleForHeapStart(),
		mChainBufferndex,
		mRTVDescriptorSize);

}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::GetDepthStencilView() const
{
	return mDX12DescriptorHeapDepthStencil->GetCPUDescriptorHandleForHeapStart();
}

void DX12Device::FlushCommandQueue()
{
	const UINT64 cmdFence = mFenceValue;
	mDX12CommandQueue->Signal(mDX12Fence.Get(), cmdFence);
	mFenceValue++;

	if (mDX12Fence->GetCompletedValue() < cmdFence)
	{
		mDX12Fence->SetEventOnCompletion(cmdFence, mEventHandle);
		WaitForSingleObject(mEventHandle, INFINITE);
	}
}
