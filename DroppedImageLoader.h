#pragma once

#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "imgui.h"

namespace DirectX {
	class ScratchImage;
}

class DroppedImageLoader {
public:
	DroppedImageLoader();
	~DroppedImageLoader();
	DroppedImageLoader(const DroppedImageLoader& Other) = delete;
	DroppedImageLoader& operator=(const DroppedImageLoader& Other) = delete;
	DroppedImageLoader(DroppedImageLoader&& Other) = delete;
	DroppedImageLoader& operator=(DroppedImageLoader&& Other) = delete;

	bool Initialize(ID3D12Device* Device, ID3D12CommandQueue* CommandQueue, ID3D12DescriptorHeap* SrvHeap, UINT SrvDescriptorSize, UINT DescriptorIndex);
	void Shutdown();
	bool LoadImageFile(const std::wstring& FilePath);
	bool HasImage() const;
	ImTextureID GetTextureId() const;
	UINT GetWidth() const;
	UINT GetHeight() const;
	std::wstring GetFilePath() const;
	std::wstring GetLastErrorMessage() const;

private:
	void CreateSynchronizationObjects();
	void WaitForGpu();
	bool LoadAndConvertToDdsInMemory(const std::wstring& FilePath);
	bool CreateTextureFromScratchImage();
	bool DecodeFileToScratchImage(const std::wstring& FilePath, DirectX::ScratchImage& ScratchImage, std::wstring& ErrorMessage) const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const;

	ID3D12Device* mDevice;
	ID3D12CommandQueue* mCommandQueue;
	ID3D12DescriptorHeap* mSrvHeap;
	UINT mSrvDescriptorSize;
	UINT mDescriptorIndex;
	UINT64 mFenceValue;
	HANDLE mFenceEvent;
	bool mInitialized;
	bool mHasImage;
	UINT mWidth;
	UINT mHeight;
	std::wstring mFilePath;
	std::wstring mLastErrorMessage;
	ImTextureID mTextureId;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	Microsoft::WRL::ComPtr<ID3D12Resource> mTextureResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> mUploadResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> mStagingResource;
};
