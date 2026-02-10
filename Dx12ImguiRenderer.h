#pragma once

#include <array>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "DroppedImageLoader.h"

class Dx12ImguiRenderer {
public:
	Dx12ImguiRenderer();
	~Dx12ImguiRenderer();
	Dx12ImguiRenderer(const Dx12ImguiRenderer& Other) = delete;
	Dx12ImguiRenderer& operator=(const Dx12ImguiRenderer& Other) = delete;
	Dx12ImguiRenderer(Dx12ImguiRenderer&& Other) = delete;
	Dx12ImguiRenderer& operator=(Dx12ImguiRenderer&& Other) = delete;

	bool Initialize(HWND WindowHandle);
	void Shutdown();
	void Resize(UINT Width, UINT Height);
	void Render();
	bool LoadDroppedImage(const std::wstring& FilePath);
	LRESULT HandleWindowMessage(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam);

private:
	void CreateDeviceResources();
	void CreateRenderTargets();
	void ReleaseRenderTargets();
	void WaitForGpu();
	void MoveToNextFrame();
	void RecordCommandList();
	void RenderImagePanel();
	bool IsInitialized() const;

	static constexpr UINT FrameCount { 2 };
	static constexpr UINT SrvDescriptorCount { 2 };
	static constexpr UINT ImageSrvDescriptorIndex { 1 };

	HWND mWindowHandle;
	UINT mFrameIndex;
	UINT mRtvDescriptorSize;
	UINT mSrvDescriptorSize;
	UINT mRenderWidth;
	UINT mRenderHeight;
	UINT64 mFenceValue;
	HANDLE mFenceEvent;
	bool mInitialized;
	DroppedImageLoader mDroppedImageLoader;
	Microsoft::WRL::ComPtr<IDXGIFactory4> mFactory;
	Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
	Microsoft::WRL::ComPtr<IDXGISwapChain3> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
	std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> mCommandAllocators;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> mRenderTargets;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
};
