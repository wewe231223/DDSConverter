#include "framework.h"
#include "DroppedImageLoader.h"

#include <cstring>
#include <vector>

#include "DirectXTex.h"

DroppedImageLoader::DroppedImageLoader()
	: mDevice			{ nullptr }
	, mCommandQueue		{ nullptr }
	, mSrvHeap			{ nullptr }
	, mSrvDescriptorSize	{ 0 }
	, mDescriptorIndex	{ 0 }
	, mFenceValue		{ 0 }
	, mFenceEvent		{ nullptr }
	, mInitialized		{ false }
	, mHasImage			{ false }
	, mWidth			{ 0 }
	, mHeight			{ 0 }
	, mTextureId		{ nullptr } {
}

DroppedImageLoader::~DroppedImageLoader() {
	Shutdown();
}

bool DroppedImageLoader::Initialize(ID3D12Device* Device, ID3D12CommandQueue* CommandQueue, ID3D12DescriptorHeap* SrvHeap, UINT SrvDescriptorSize, UINT DescriptorIndex) {
	if (mInitialized) {
		return true;
	}
	if (Device == nullptr || CommandQueue == nullptr || SrvHeap == nullptr || SrvDescriptorSize == 0) {
		return false;
	}
	mDevice = Device;
	mCommandQueue = CommandQueue;
	mSrvHeap = SrvHeap;
	mSrvDescriptorSize = SrvDescriptorSize;
	mDescriptorIndex = DescriptorIndex;
	mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocator));
	mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&mCommandList));
	mCommandList->Close();
	CreateSynchronizationObjects();
	mInitialized = mFenceEvent != nullptr;
	return mInitialized;
}

void DroppedImageLoader::Shutdown() {
	if (mCommandQueue != nullptr && mFence != nullptr && mFenceEvent != nullptr) {
		WaitForGpu();
	}
	mTextureResource.Reset();
	mUploadResource.Reset();
	mStagingResource.Reset();
	mFence.Reset();
	mCommandList.Reset();
	mCommandAllocator.Reset();
	if (mFenceEvent != nullptr) {
		CloseHandle(mFenceEvent);
		mFenceEvent = nullptr;
	}
	mTextureId = nullptr;
	mFilePath.clear();
	mWidth = 0;
	mHeight = 0;
	mHasImage = false;
	mInitialized = false;
	mFenceValue = 0;
	mDescriptorIndex = 0;
	mSrvDescriptorSize = 0;
	mSrvHeap = nullptr;
	mCommandQueue = nullptr;
	mDevice = nullptr;
}

bool DroppedImageLoader::LoadImageFile(const std::wstring& FilePath) {
	if (!mInitialized) {
		return false;
	}
	if (!LoadScratchImage(FilePath)) {
		return false;
	}
	if (!CreateTextureFromScratchImage()) {
		return false;
	}
	mFilePath = FilePath;
	mHasImage = true;
	return true;
}

bool DroppedImageLoader::HasImage() const {
	return mHasImage;
}

ImTextureID DroppedImageLoader::GetTextureId() const {
	return mTextureId;
}

UINT DroppedImageLoader::GetWidth() const {
	return mWidth;
}

UINT DroppedImageLoader::GetHeight() const {
	return mHeight;
}

std::wstring DroppedImageLoader::GetFilePath() const {
	return mFilePath;
}

void DroppedImageLoader::CreateSynchronizationObjects() {
	mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
	mFenceValue = 1;
	mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DroppedImageLoader::WaitForGpu() {
	const UINT64 FenceToWaitFor { mFenceValue };
	mCommandQueue->Signal(mFence.Get(), FenceToWaitFor);
	mFenceValue += 1;
	if (mFence->GetCompletedValue() < FenceToWaitFor) {
		mFence->SetEventOnCompletion(FenceToWaitFor, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);
	}
}

bool DroppedImageLoader::LoadScratchImage(const std::wstring& FilePath) {
	DirectX::TexMetadata Metadata {};
	DirectX::ScratchImage ScratchImage {};
	HRESULT LoadResult { DirectX::LoadFromWICFile(FilePath.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &Metadata, ScratchImage) };
	if (FAILED(LoadResult)) {
		LoadResult = DirectX::LoadFromDDSFile(FilePath.c_str(), DirectX::DDS_FLAGS_FORCE_RGB, &Metadata, ScratchImage);
	}
	if (FAILED(LoadResult)) {
		return false;
	}
	DirectX::ScratchImage ConvertedImage {};
	HRESULT ConvertResult { DirectX::Convert(*ScratchImage.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, ConvertedImage) };
	if (FAILED(ConvertResult)) {
		return false;
	}
	mStagingResource.Reset();
	mUploadResource.Reset();
	mTextureResource.Reset();
	mWidth = static_cast<UINT>(ConvertedImage.GetMetadata().width);
	mHeight = static_cast<UINT>(ConvertedImage.GetMetadata().height);
	const DirectX::Image* ImageData { ConvertedImage.GetImage(0, 0, 0) };
	if (ImageData == nullptr) {
		return false;
	}
	D3D12_RESOURCE_DESC TextureDesc {};
	TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	TextureDesc.Alignment = 0;
	TextureDesc.Width = ImageData->width;
	TextureDesc.Height = static_cast<UINT>(ImageData->height);
	TextureDesc.DepthOrArraySize = 1;
	TextureDesc.MipLevels = 1;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	TextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_HEAP_PROPERTIES TextureHeapProperties {};
	TextureHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	TextureHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	TextureHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	TextureHeapProperties.CreationNodeMask = 1;
	TextureHeapProperties.VisibleNodeMask = 1;
	mDevice->CreateCommittedResource(&TextureHeapProperties, D3D12_HEAP_FLAG_NONE, &TextureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mTextureResource));
	UINT64 UploadBufferSize { 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint {};
	UINT NumRows { 0 };
	UINT64 RowSizeInBytes { 0 };
	mDevice->GetCopyableFootprints(&TextureDesc, 0, 1, 0, &Footprint, &NumRows, &RowSizeInBytes, &UploadBufferSize);
	D3D12_RESOURCE_DESC UploadDesc {};
	UploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	UploadDesc.Alignment = 0;
	UploadDesc.Width = UploadBufferSize;
	UploadDesc.Height = 1;
	UploadDesc.DepthOrArraySize = 1;
	UploadDesc.MipLevels = 1;
	UploadDesc.Format = DXGI_FORMAT_UNKNOWN;
	UploadDesc.SampleDesc.Count = 1;
	UploadDesc.SampleDesc.Quality = 0;
	UploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	UploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_HEAP_PROPERTIES UploadHeapProperties {};
	UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	UploadHeapProperties.CreationNodeMask = 1;
	UploadHeapProperties.VisibleNodeMask = 1;
	mDevice->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &UploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mUploadResource));
	std::vector<unsigned char> RawPixels {};
	RawPixels.resize(ImageData->slicePitch);
	memcpy(RawPixels.data(), ImageData->pixels, ImageData->slicePitch);
	void* MappedData { nullptr };
	D3D12_RANGE ReadRange {};
	ReadRange.Begin = 0;
	ReadRange.End = 0;
	mUploadResource->Map(0, &ReadRange, &MappedData);
	unsigned char* DestBytes { static_cast<unsigned char*>(MappedData) };
	for (UINT RowIndex { 0 }; RowIndex < NumRows; ++RowIndex) {
		const SIZE_T SrcOffset { static_cast<SIZE_T>(RowIndex) * ImageData->rowPitch };
		const SIZE_T DstOffset { static_cast<SIZE_T>(RowIndex) * Footprint.Footprint.RowPitch };
		memcpy(DestBytes + DstOffset, RawPixels.data() + SrcOffset, ImageData->rowPitch);
	}
	mUploadResource->Unmap(0, nullptr);
	mCommandAllocator->Reset();
	mCommandList->Reset(mCommandAllocator.Get(), nullptr);
	D3D12_TEXTURE_COPY_LOCATION DstLocation {};
	DstLocation.pResource = mTextureResource.Get();
	DstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	DstLocation.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION SrcLocation {};
	SrcLocation.pResource = mUploadResource.Get();
	SrcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	SrcLocation.PlacedFootprint = Footprint;
	mCommandList->CopyTextureRegion(&DstLocation, 0, 0, 0, &SrcLocation, nullptr);
	D3D12_RESOURCE_BARRIER Barrier {};
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = mTextureResource.Get();
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &Barrier);
	mCommandList->Close();
	ID3D12CommandList* CommandLists[] { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, CommandLists);
	WaitForGpu();
	return true;
}

bool DroppedImageLoader::CreateTextureFromScratchImage() {
	if (mTextureResource == nullptr) {
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc {};
	ShaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	ShaderResourceViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ShaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	ShaderResourceViewDesc.Texture2D.MipLevels = 1;
	ShaderResourceViewDesc.Texture2D.PlaneSlice = 0;
	ShaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0F;
	D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle { GetCpuHandle() };
	mDevice->CreateShaderResourceView(mTextureResource.Get(), &ShaderResourceViewDesc, CpuHandle);
	D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle { GetGpuHandle() };
	mTextureId = reinterpret_cast<ImTextureID>(GpuHandle.ptr);
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE DroppedImageLoader::GetCpuHandle() const {
	D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle { mSrvHeap->GetCPUDescriptorHandleForHeapStart() };
	CpuHandle.ptr += static_cast<SIZE_T>(mDescriptorIndex) * static_cast<SIZE_T>(mSrvDescriptorSize);
	return CpuHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DroppedImageLoader::GetGpuHandle() const {
	D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle { mSrvHeap->GetGPUDescriptorHandleForHeapStart() };
	GpuHandle.ptr += static_cast<UINT64>(mDescriptorIndex) * static_cast<UINT64>(mSrvDescriptorSize);
	return GpuHandle;
}
