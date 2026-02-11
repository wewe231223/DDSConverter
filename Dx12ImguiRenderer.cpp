#include "framework.h"
#include "Dx12ImguiRenderer.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam);

Dx12ImguiRenderer::Dx12ImguiRenderer()
	: mWindowHandle{ nullptr }
	, mFrameIndex{ 0 }
	, mRtvDescriptorSize{ 0 }
	, mSrvDescriptorSize{ 0 }
	, mRenderWidth{ 0 }
	, mRenderHeight{ 0 }
	, mFenceValue{ 0 }
	, mFenceEvent{ nullptr }
	, mInitialized{ false }
	, mNeedConversionRefresh{ false }
	, mImageStatusMessage{}
	, mConversionOptions{ DdsOutputFormat::Bc7Unorm, true, false, false, 0.5F, 1.0F, 1.0F, 1.0F } {
}

Dx12ImguiRenderer::~Dx12ImguiRenderer() {
	Shutdown();
}

bool Dx12ImguiRenderer::Initialize(HWND WindowHandle) {
	if (mInitialized) {
		return true;
	}
	mWindowHandle = WindowHandle;
	RECT ClientRect{};
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
	ImGuiIO& Io{ ImGui::GetIO() };
	const char* FontPath{ "C:\\Windows\\Fonts\\malgun.ttf" };
	const ImWchar* KoreanRange{ Io.Fonts->GetGlyphRangesKorean() };
	ImFontConfig FontConfig{};
	FontConfig.OversampleH = 2;
	FontConfig.OversampleV = 2;
	FontConfig.PixelSnapH = true;
	ImFont* Font{ Io.Fonts->AddFontFromFileTTF(FontPath, 18.0F, &FontConfig, KoreanRange) };
	if (Font == nullptr) {
		Io.Fonts->AddFontDefault();
	}
	ImGui::StyleColorsDark();
	if (!ImGui_ImplWin32_Init(mWindowHandle)) {
		mImageStatusMessage = "ImGui Win32 초기화에 실패했습니다.";
		return false;
	}
	if (!ImGui_ImplDX12_Init(mDevice.Get(), FrameCount, DXGI_FORMAT_R8G8B8A8_UNORM, mSrvHeap.Get(), mSrvHeap->GetCPUDescriptorHandleForHeapStart(), mSrvHeap->GetGPUDescriptorHandleForHeapStart())) {
		mImageStatusMessage = "ImGui DX12 초기화에 실패했습니다.";
		return false;
	}
	if (!mDroppedImageLoader.Initialize(mDevice.Get(), mCommandQueue.Get(), mSrvHeap.Get(), mSrvDescriptorSize, SourceImageSrvDescriptorIndex, ConvertedImageSrvDescriptorIndex)) {
		mImageStatusMessage = mDroppedImageLoader.GetLastErrorMessage();
		return false;
	}
	mInitialized = true;
	mImageStatusMessage = "이미지를 드롭한 뒤 옵션을 조정하면 DDS 미리보기가 즉시 갱신됩니다.";
	return true;
}

void Dx12ImguiRenderer::Shutdown() {
	if (!mInitialized && mDevice == nullptr) {
		return;
	}
	if (mInitialized) {
		WaitForGpu();
		mDroppedImageLoader.Shutdown();
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
	mImageStatusMessage.clear();
	mInitialized = false;
}

void Dx12ImguiRenderer::Resize(UINT Width, UINT Height) {
	if (!mInitialized || Width == 0 || Height == 0) {
		return;
	}
	WaitForGpu();
	ReleaseRenderTargets();
	DXGI_SWAP_CHAIN_DESC SwapChainDesc{};
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
	RenderPanels();
	ImGui::Render();
	RecordCommandList();
	ID3D12CommandList* CommandLists[]{ mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, CommandLists);
	mSwapChain->Present(1, 0);
	MoveToNextFrame();
}

bool Dx12ImguiRenderer::LoadDroppedImage(const std::wstring& FilePath) {
	if (!mInitialized) {
		mImageStatusMessage = "렌더러가 초기화되지 않아 이미지를 로드할 수 없습니다.";
		return false;
	}
	const bool LoadResult{ mDroppedImageLoader.LoadImageFile(FilePath, mConversionOptions) };
	if (!LoadResult) {
		mImageStatusMessage = mDroppedImageLoader.GetLastErrorMessage();
		if (mImageStatusMessage.empty()) {
			mImageStatusMessage = "이미지 로드에 실패했습니다.";
		}
		return false;
	}
	mImageStatusMessage = "원본/변환 이미지 미리보기를 갱신했습니다.";
	return true;
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
	D3D12_COMMAND_QUEUE_DESC QueueDesc{};
	QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	mDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&mCommandQueue));
	DXGI_SWAP_CHAIN_DESC1 SwapChainDesc{};
	SwapChainDesc.BufferCount = FrameCount;
	SwapChainDesc.Width = mRenderWidth;
	SwapChainDesc.Height = mRenderHeight;
	SwapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	SwapChainDesc.SampleDesc.Count = 1;
	Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain1{};
	mFactory->CreateSwapChainForHwnd(mCommandQueue.Get(), mWindowHandle, &SwapChainDesc, nullptr, nullptr, &SwapChain1);
	SwapChain1.As(&mSwapChain);
	mFrameIndex = mSwapChain->GetCurrentBackBufferIndex();
	D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc{};
	RtvHeapDesc.NumDescriptors = FrameCount;
	RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	RtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	mDevice->CreateDescriptorHeap(&RtvHeapDesc, IID_PPV_ARGS(&mRtvHeap));
	mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_DESCRIPTOR_HEAP_DESC SrvHeapDesc{};
	SrvHeapDesc.NumDescriptors = SrvDescriptorCount;
	SrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	SrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	mDevice->CreateDescriptorHeap(&SrvHeapDesc, IID_PPV_ARGS(&mSrvHeap));
	mSrvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (UINT Index{ 0 }; Index < FrameCount; ++Index) {
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
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle{ mRtvHeap->GetCPUDescriptorHandleForHeapStart() };
	for (UINT Index{ 0 }; Index < FrameCount; ++Index) {
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
	const UINT64 FenceToWaitFor{ mFenceValue };
	mCommandQueue->Signal(mFence.Get(), FenceToWaitFor);
	mFenceValue += 1;
	if (mFence->GetCompletedValue() < FenceToWaitFor) {
		mFence->SetEventOnCompletion(FenceToWaitFor, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);
	}
}

void Dx12ImguiRenderer::MoveToNextFrame() {
	const UINT64 CurrentFenceValue{ mFenceValue };
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
	D3D12_RESOURCE_BARRIER ToRenderTargetBarrier{};
	ToRenderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	ToRenderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	ToRenderTargetBarrier.Transition.pResource = mRenderTargets[mFrameIndex].Get();
	ToRenderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	ToRenderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	ToRenderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &ToRenderTargetBarrier);
	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle{ mRtvHeap->GetCPUDescriptorHandleForHeapStart() };
	RtvHandle.ptr += static_cast<SIZE_T>(mFrameIndex) * static_cast<SIZE_T>(mRtvDescriptorSize);
	const FLOAT ClearColor[4]{ 0.1F, 0.1F, 0.1F, 1.0F };
	mCommandList->OMSetRenderTargets(1, &RtvHandle, FALSE, nullptr);
	mCommandList->ClearRenderTargetView(RtvHandle, ClearColor, 0, nullptr);
	ID3D12DescriptorHeap* DescriptorHeaps[]{ mSrvHeap.Get() };
	mCommandList->SetDescriptorHeaps(1, DescriptorHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
	D3D12_RESOURCE_BARRIER ToPresentBarrier{};
	ToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	ToPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	ToPresentBarrier.Transition.pResource = mRenderTargets[mFrameIndex].Get();
	ToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	ToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	ToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &ToPresentBarrier);
	mCommandList->Close();
}

void Dx12ImguiRenderer::RenderPanels() {
	RenderOptionPanel();
	RenderSourceImagePanel();
	RenderConvertedImagePanel();
	if (mNeedConversionRefresh && mDroppedImageLoader.HasImage()) {
		ApplyConversionOptions();
		mNeedConversionRefresh = false;
	}
}

void Dx12ImguiRenderer::RenderOptionPanel() {
	ImGui::Begin("DDS 옵션");
	ImGui::Text("화면 크기: %u x %u", mRenderWidth, mRenderHeight);
	ImGui::Text("이미지 드롭 후 옵션을 조정하세요.");
	if (!mImageStatusMessage.empty()) {
		ImGui::Text("상태: %s", mImageStatusMessage.c_str());
	}
	int SelectedFormatIndex{ static_cast<int>(mConversionOptions.OutputFormat) };
	if (ImGui::BeginCombo("출력 포맷", GetOutputFormatName(mConversionOptions.OutputFormat))) {
		for (int Index{ 0 }; Index <= static_cast<int>(DdsOutputFormat::Rgba8UnormSrgb); ++Index) {
			const DdsOutputFormat Format{ static_cast<DdsOutputFormat>(Index) };
			const bool IsSelected{ SelectedFormatIndex == Index };
			if (ImGui::Selectable(GetOutputFormatName(Format), IsSelected)) {
				SelectedFormatIndex = Index;
				mNeedConversionRefresh = true;
			}
			if (IsSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (SelectedFormatIndex != static_cast<int>(mConversionOptions.OutputFormat)) {
		mConversionOptions.OutputFormat = static_cast<DdsOutputFormat>(SelectedFormatIndex);
	}
	if (ImGui::Checkbox("밉맵 생성", &mConversionOptions.GenerateMipMaps)) {
		mNeedConversionRefresh = true;
	}
	if (ImGui::Checkbox("디더링", &mConversionOptions.EnableDithering)) {
		mNeedConversionRefresh = true;
	}
	if (ImGui::Checkbox("균일 가중치", &mConversionOptions.UseUniformWeighting)) {
		mNeedConversionRefresh = true;
	}
	if (ImGui::SliderFloat("알파 기준값", &mConversionOptions.AlphaReference, 0.0F, 1.0F, "%.3f")) {
		mNeedConversionRefresh = true;
	}
	if (ImGui::SliderFloat("가중치 R", &mConversionOptions.CompressionWeightRed, 0.0F, 2.0F, "%.3f")) {
		mNeedConversionRefresh = true;
	}
	if (ImGui::SliderFloat("가중치 G", &mConversionOptions.CompressionWeightGreen, 0.0F, 2.0F, "%.3f")) {
		mNeedConversionRefresh = true;
	}
	if (ImGui::SliderFloat("가중치 B", &mConversionOptions.CompressionWeightBlue, 0.0F, 2.0F, "%.3f")) {
		mNeedConversionRefresh = true;
	}
	ImGui::End();
}

void Dx12ImguiRenderer::RenderSourceImagePanel() {
	ImGui::Begin("원본 이미지");
	if (!mDroppedImageLoader.HasImage()) {
		ImGui::Text("이미지를 드롭하면 원본 미리보기가 표시됩니다.");
		ImGui::End();
		return;
	}
	ImGui::Text("경로: %ls", mDroppedImageLoader.GetFilePath().c_str());
	ImGui::Text("원본 크기: %u x %u", mDroppedImageLoader.GetSourceWidth(), mDroppedImageLoader.GetSourceHeight());
	DrawImagePreview(mDroppedImageLoader.GetSourceTextureId(), mDroppedImageLoader.GetSourceWidth(), mDroppedImageLoader.GetSourceHeight());
	ImGui::End();
}

void Dx12ImguiRenderer::RenderConvertedImagePanel() {
	ImGui::Begin("DDS 변환 결과");
	if (!mDroppedImageLoader.HasImage()) {
		ImGui::Text("이미지를 드롭하면 DDS 변환 결과가 표시됩니다.");
		ImGui::End();
		return;
	}
	ImGui::Text("선택 포맷: %s", GetOutputFormatName(mConversionOptions.OutputFormat));
	ImGui::Text("DDS 표시 크기: %u x %u", mDroppedImageLoader.GetConvertedWidth(), mDroppedImageLoader.GetConvertedHeight());
	DrawImagePreview(mDroppedImageLoader.GetConvertedTextureId(), mDroppedImageLoader.GetConvertedWidth(), mDroppedImageLoader.GetConvertedHeight());
	ImGui::End();
}

void Dx12ImguiRenderer::DrawImagePreview(ImTextureID TextureId, UINT Width, UINT Height) {
	const float AvailableWidth{ ImGui::GetContentRegionAvail().x };
	const float MaxDisplayWidth{ AvailableWidth > 32.0F ? AvailableWidth : 512.0F };
	float DisplayWidth{ static_cast<float>(Width) };
	float DisplayHeight{ static_cast<float>(Height) };
	if (DisplayWidth > MaxDisplayWidth && DisplayWidth > 0.0F) {
		const float Scale{ MaxDisplayWidth / DisplayWidth };
		DisplayWidth *= Scale;
		DisplayHeight *= Scale;
	}
	ImGui::Image(TextureId, ImVec2(DisplayWidth, DisplayHeight));
}

bool Dx12ImguiRenderer::IsInitialized() const {
	return mInitialized;
}

const char* Dx12ImguiRenderer::GetOutputFormatName(DdsOutputFormat OutputFormat) const {
	switch (OutputFormat) {
	case DdsOutputFormat::Bc1Unorm:
		return "BC1 UNORM";
	case DdsOutputFormat::Bc1UnormSrgb:
		return "BC1 UNORM SRGB";
	case DdsOutputFormat::Bc2Unorm:
		return "BC2 UNORM";
	case DdsOutputFormat::Bc2UnormSrgb:
		return "BC2 UNORM SRGB";
	case DdsOutputFormat::Bc3Unorm:
		return "BC3 UNORM";
	case DdsOutputFormat::Bc3UnormSrgb:
		return "BC3 UNORM SRGB";
	case DdsOutputFormat::Bc4Unorm:
		return "BC4 UNORM";
	case DdsOutputFormat::Bc4Snorm:
		return "BC4 SNORM";
	case DdsOutputFormat::Bc5Unorm:
		return "BC5 UNORM";
	case DdsOutputFormat::Bc5Snorm:
		return "BC5 SNORM";
	case DdsOutputFormat::Bc6hUf16:
		return "BC6H UF16";
	case DdsOutputFormat::Bc6hSf16:
		return "BC6H SF16";
	case DdsOutputFormat::Bc7Unorm:
		return "BC7 UNORM";
	case DdsOutputFormat::Bc7UnormSrgb:
		return "BC7 UNORM SRGB";
	case DdsOutputFormat::Rgba8Unorm:
		return "R8G8B8A8 UNORM";
	case DdsOutputFormat::Rgba8UnormSrgb:
		return "R8G8B8A8 UNORM SRGB";
	default:
		return "BC7 UNORM";
	}
}

bool Dx12ImguiRenderer::ApplyConversionOptions() {
	if (!mDroppedImageLoader.RebuildConvertedImage(mConversionOptions)) {
		mImageStatusMessage = mDroppedImageLoader.GetLastErrorMessage();
		if (mImageStatusMessage.empty()) {
			mImageStatusMessage = "옵션 변경 반영에 실패했습니다.";
		}
		return false;
	}
	mImageStatusMessage = "옵션 변경을 반영하여 DDS 미리보기를 다시 생성했습니다.";
	return true;
}
