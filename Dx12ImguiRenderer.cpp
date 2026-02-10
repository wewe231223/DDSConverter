#include "framework.h"
#include "Dx12ImguiRenderer.h"

#include <stdexcept>

#include "External/ImGui/imgui.h"
#include "External/ImGui/imgui_impl_dx12.h"
#include "External/ImGui/imgui_impl_win32.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam);

Dx12ImguiRenderer::Dx12ImguiRenderer()
	: mWindowHandle		{ nullptr }
	, mFrameIndex		{ 0 }
	, mRtvDescriptorSize	{ 0 }
	, mRenderWidth		{ 0 }
	, mRenderHeight		{ 0 }
	, mFenceValue		{ 0 }
	, mFenceEvent		{ nullptr }
	, mInitialized		{ false } {
}

Dx12ImguiRenderer::~Dx12ImguiRenderer() {
	Shutdown();
}

bool Dx12ImguiRenderer::Initialize(HWND WindowHandle) {
	if (mInitialized) {
		return true;
	}
	mWindowHandle = WindowHandle;
	RECT ClientRect {};
	GetClientRect(mWindowHandle, &ClientRect);
	mRenderWidth = static_cast<UINT>(ClientRect.right - ClientRect.left);
	mRenderHeight = static_cast<UINT>(ClientRect.bottom - ClientRect.top);
	if (mRenderWidth == 0 || mRenderHeight == 0) {
		mRenderWidth = 1280;
		mRenderHeight = 720;
	}
	CreateDeviceResources();
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	if (!ImGui_ImplWin32_Init(mWindowHandle)) {
		return false;
	}
	ImGui_ImplDX12_InitInfo InitInfo {};
	InitInfo.Device = mDevice.Get();
	InitInfo.CommandQueue = mCommandQueue.Get();
	InitInfo.NumFramesInFlight = FrameCount;
	InitInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	InitInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	InitInfo.SrvDescriptorHeap = mSrvHeap.Get();
	InitInfo.SrvDescriptorAllocFn = nullptr;
	InitInfo.SrvDescriptorFreeFn = nullptr;
	InitInfo.LegacySingleSrvCpuDescriptor = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
	InitInfo.LegacySingleSrvGpuDescriptor = mSrvHeap->GetGPUDescriptorHandleForHeapStart();
	if (!ImGui_ImplDX12_Init(&InitInfo)) {
		return false;
	}
	mInitialized = true;
	return true;
}

void Dx12ImguiRenderer::Shutdown() {
	if (!mInitialized && mDevice == nullptr) {
		return;
	}
	if (mInitialized) {
		WaitForGpu();
		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
	ReleaseRenderTargets();
	if (mFenceEvent != nullptr) {
		CloseHandle(mFenceEvent);
		mFenceEvent = nullptr;
	}
	mCommandList.Reset();
	for (auto& Allocator : mCommandAllocators) {
		Allocator.Reset();
	}
	mSrvHeap.Reset();
	mRtvHeap.Reset();
	mSwapChain.Reset();
	mCommandQueue.Reset();
	mFence.Reset();
	mDevice.Reset();
	mFactory.Reset();
	mInitialized = false;
}

void Dx12ImguiRenderer::Resize(UINT Width, UINT Height) {
	if (!mInitialized || Width == 0 || Height == 0) {
		return;
	}
	WaitForGpu();
	ReleaseRenderTargets();
	DXGI_SWAP_CHAIN_DESC SwapChainDesc {};
	mSwapChain->GetDesc(&SwapChainDesc);
	mSwapChain->ResizeBuffers(FrameCount, Width, Height, SwapChainDesc.BufferDesc.Format, SwapChainDesc.Flags);
	mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
	mRenderWidth = Width;
	mRenderHeight = Height;
	CreateRenderTargets();
}

void Dx12ImguiRenderer::Render() {
	if (!IsInitialized()) {
		return;
	}
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::Begin("DirectX12 ImGui Renderer");
	ImGui::Text("DDS Converter");
	ImGui::Text("Width: %u", mRenderWidth);
	ImGui::Text("Height: %u", mRenderHeight);
	ImGui::End();
	ImGui::Render();
	RecordCommandList();
	ID3D12CommandList* CommandLists[] { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, CommandLists);
	mSwapChain->Present(1, 0);
	MoveToNextFrame();
}

LRESULT Dx12ImguiRenderer::HandleWindowMessage(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam) {
	if (ImGui_ImplWin32_WndProcHandler(WindowHandle, Message, WParam, LParam) != 0) {
		return TRUE;
	}
	return 0;
}

void Dx12ImguiRenderer::CreateDeviceResources() {
	CreateDXGIFactory1(IID_PPV_ARGS(&mFactory));
	D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice));
	D3D12_COMMAND_QUEUE_DESC QueueDesc {};
	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	mDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&mCommandQueue));
	DXGI_SWAP_CHAIN_DESC1 SwapChainDesc {};
	SwapChainDesc.BufferCount = FrameCount;
	SwapChainDesc.Width = mRenderWidth;
	SwapChainDesc.Height = mRenderHeight;
	SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.SampleDesc.Count = 1;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain1 {};
	mFactory->CreateSwapChainForHwnd(mCommandQueue.Get(), mWindowHandle, &SwapChainDesc, nullptr, nullptr, &SwapChain1);
	SwapChain1.As(&mSwapChain);
	mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
	D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc {};
	RtvHeapDesc.NumDescriptors = FrameCount;
	RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	RtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	mDevice->CreateDescriptorHeap(&RtvHeapDesc, IID_PPV_ARGS(&mRtvHeap));
	mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_DESCRIPTOR_HEAP_DESC SrvHeapDesc {};
	SrvHeapDesc.NumDescriptors = 1;
	SrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	SrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	mDevice->CreateDescriptorHeap(&SrvHeapDesc, IID_PPV_ARGS(&mSrvHeap));
	for (UINT Index { 0 }; Index < FrameCount; ++Index) {
		mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocators[Index]));
	}
	mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocators[mFrameIndex].Get(), nullptr, IID_PPV_ARGS(&mCommandList));
	mCommandList->Close();
	CreateRenderTargets();
	mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
	mFenceValue = 1;
	mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void Dx12ImguiRenderer::CreateRenderTargets() {
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle { mRtvHeap->GetCPUDescriptorHandleForHeapStart() };
	for (UINT Index { 0 }; Index < FrameCount; ++Index) {
		mSwapChain->GetBuffer(Index, IID_PPV_ARGS(&mRenderTargets[Index]));
		mDevice->CreateRenderTargetView(mRenderTargets[Index].Get(), nullptr, RtvHandle);
		RtvHandle.ptr += mRtvDescriptorSize;
	}
}

void Dx12ImguiRenderer::ReleaseRenderTargets() {
	for (auto& Target : mRenderTargets) {
		Target.Reset();
	}
}

void Dx12ImguiRenderer::WaitForGpu() {
	if (mCommandQueue == nullptr || mFence == nullptr) {
		return;
	}
	const UINT64 FenceToWaitFor { mFenceValue };
	mCommandQueue->Signal(mFence.Get(), FenceToWaitFor);
	mFenceValue += 1;
	if (mFence->GetCompletedValue() < FenceToWaitFor) {
		mFence->SetEventOnCompletion(FenceToWaitFor, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);
	}
}

void Dx12ImguiRenderer::MoveToNextFrame() {
	const UINT64 CurrentFenceValue { mFenceValue };
	mCommandQueue->Signal(mFence.Get(), CurrentFenceValue);
	mFenceValue += 1;
	if (mFence->GetCompletedValue() < CurrentFenceValue) {
		mFence->SetEventOnCompletion(CurrentFenceValue, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);
	}
	mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
}

void Dx12ImguiRenderer::RecordCommandList() {
	mCommandAllocators[mFrameIndex]->Reset();
	mCommandList->Reset(mCommandAllocators[mFrameIndex].Get(), nullptr);
	D3D12_RESOURCE_BARRIER ToRenderTargetBarrier {};
	ToRenderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	ToRenderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	ToRenderTargetBarrier.Transition.pResource = mRenderTargets[mFrameIndex].Get();
	ToRenderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	ToRenderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	ToRenderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &ToRenderTargetBarrier);
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle { mRtvHeap->GetCPUDescriptorHandleForHeapStart() };
	RtvHandle.ptr += static_cast<SIZE_T>(mFrameIndex) * static_cast<SIZE_T>(mRtvDescriptorSize);
	const FLOAT ClearColor[4] { 0.1F, 0.1F, 0.1F, 1.0F };
	mCommandList->OMSetRenderTargets(1, &RtvHandle, FALSE, nullptr);
	mCommandList->ClearRenderTargetView(RtvHandle, ClearColor, 0, nullptr);
	ID3D12DescriptorHeap* DescriptorHeaps[] { mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(1, DescriptorHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
	D3D12_RESOURCE_BARRIER ToPresentBarrier {};
	ToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	ToPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	ToPresentBarrier.Transition.pResource = mRenderTargets[mFrameIndex].Get();
	ToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	ToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	ToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &ToPresentBarrier);
	mCommandList->Close();
}

bool Dx12ImguiRenderer::IsInitialized() const {
	return mInitialized;
}
