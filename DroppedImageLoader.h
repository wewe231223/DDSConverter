#pragma once

#include <memory>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "imgui.h"

namespace DirectX {
	class ScratchImage;
}

enum class DdsOutputFormat {
	Bc1Unorm,
	Bc1UnormSrgb,
	Bc2Unorm,
	Bc2UnormSrgb,
	Bc3Unorm,
	Bc3UnormSrgb,
	Bc4Unorm,
	Bc4Snorm,
	Bc5Unorm,
	Bc5Snorm,
	Bc6hUf16,
	Bc6hSf16,
	Bc7Unorm,
	Bc7UnormSrgb,
	Rgba8Unorm,
	Rgba8UnormSrgb
};

struct DdsConversionOptions {
	DdsOutputFormat OutputFormat;
	bool GenerateMipMaps;
	bool EnableDithering;
	bool UseUniformWeighting;
	float AlphaReference;
	float CompressionWeightRed;
	float CompressionWeightGreen;
	float CompressionWeightBlue;
};

class DroppedImageLoader {
public:
	DroppedImageLoader();
	~DroppedImageLoader();
	DroppedImageLoader(const DroppedImageLoader& Other) = delete;
	DroppedImageLoader& operator=(const DroppedImageLoader& Other) = delete;
	DroppedImageLoader(DroppedImageLoader&& Other) = delete;
	DroppedImageLoader& operator=(DroppedImageLoader&& Other) = delete;

	bool Initialize(ID3D12Device* Device, ID3D12CommandQueue* CommandQueue, ID3D12DescriptorHeap* SrvHeap, UINT SrvDescriptorSize, UINT SourceDescriptorIndex, UINT ConvertedDescriptorIndex);
	void Shutdown();
	bool LoadImageFile(const std::wstring& FilePath, const DdsConversionOptions& ConversionOptions);
	bool RebuildConvertedImage(const DdsConversionOptions& ConversionOptions);
	bool HasImage() const;
	ImTextureID GetSourceTextureId() const;
	ImTextureID GetConvertedTextureId() const;
	UINT GetSourceWidth() const;
	UINT GetSourceHeight() const;
	UINT GetConvertedWidth() const;
	UINT GetConvertedHeight() const;
	std::wstring GetFilePath() const;
	std::string GetLastErrorMessage() const;

private:
	void CreateSynchronizationObjects();
	void WaitForGpu();
	bool DecodeFileToScratchImage(const std::wstring& FilePath, DirectX::ScratchImage& ScratchImage, std::string& ErrorMessage) const;
	bool BuildPreviewScratchImage(const DirectX::ScratchImage& SourceScratchImage, bool IsSrgbTarget, DirectX::ScratchImage& PreviewScratchImage, std::string& ErrorMessage) const;
	bool BuildConvertedScratchImage(const DdsConversionOptions& ConversionOptions, DirectX::ScratchImage& ConvertedScratchImage, std::string& ErrorMessage) const;
	bool UploadTextureToDescriptor(const DirectX::ScratchImage& ScratchImage, UINT DescriptorIndex, Microsoft::WRL::ComPtr<ID3D12Resource>& TextureResource, Microsoft::WRL::ComPtr<ID3D12Resource>& UploadResource, ImTextureID& TextureId, UINT& Width, UINT& Height, std::string& ErrorMessage);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT DescriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT DescriptorIndex) const;
	DXGI_FORMAT GetDxgiFormat(DdsOutputFormat OutputFormat) const;
	DWORD GetCompressFlags(const DdsConversionOptions& ConversionOptions) const;

	ID3D12Device* mDevice;
	ID3D12CommandQueue* mCommandQueue;
	ID3D12DescriptorHeap* mSrvHeap;
	UINT mSrvDescriptorSize;
	UINT mSourceDescriptorIndex;
	UINT mConvertedDescriptorIndex;
	UINT64 mFenceValue;
	HANDLE mFenceEvent;
	bool mInitialized;
	bool mHasImage;
	UINT mSourceWidth;
	UINT mSourceHeight;
	UINT mConvertedWidth;
	UINT mConvertedHeight;
	std::wstring mFilePath;
	std::string mLastErrorMessage;
	ImTextureID mSourceTextureId;
	ImTextureID mConvertedTextureId;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSourceTextureResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSourceUploadResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> mConvertedTextureResource;
	Microsoft::WRL::ComPtr<ID3D12Resource> mConvertedUploadResource;
	std::unique_ptr<DirectX::ScratchImage> mOriginalScratchImage;
};
