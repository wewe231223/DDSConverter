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
	, mFilePath			{ }
	, mLastErrorMessage	{ }
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
		mLastErrorMessage = "DirectX12 초기화 파라미터가 올바르지 않습니다.";
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
	if (!mInitialized) {
		mLastErrorMessage = "동기화 객체 생성에 실패했습니다.";
	}
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
	mLastErrorMessage.clear();
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
		mLastErrorMessage = "이미지 로더가 초기화되지 않았습니다.";
		return false;
	}
	if (!LoadAndConvertToDdsInMemory(FilePath)) {
		mHasImage = false;
		mTextureId = nullptr;
		mFilePath.clear();
		mWidth = 0;
		mHeight = 0;
		return false;
	}
	if (!CreateTextureFromScratchImage()) {
		mHasImage = false;
		mTextureId = nullptr;
		mFilePath.clear();
		mWidth = 0;
		mHeight = 0;
		mLastErrorMessage = "DDS 텍스처 SRV 생성에 실패했습니다.";
		return false;
	}
	mFilePath = FilePath;
	mHasImage = true;
	mLastErrorMessage.clear();
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

std::string DroppedImageLoader::GetLastErrorMessage() const {
	return mLastErrorMessage;
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

bool DroppedImageLoader::DecodeFileToScratchImage(const std::wstring& FilePath, DirectX::ScratchImage& ScratchImage, std::string& ErrorMessage) const {
	DirectX::TexMetadata Metadata {};
	HRESULT LoadResult { DirectX::LoadFromWICFile(FilePath.c_str(), DirectX::WIC_FLAGS_NONE, &Metadata, ScratchImage) };
	if (SUCCEEDED(LoadResult)) {
		return true;
	}
	LoadResult = DirectX::LoadFromDDSFile(FilePath.c_str(), DirectX::DDS_FLAGS_NONE, &Metadata, ScratchImage);
	if (SUCCEEDED(LoadResult)) {
		return true;
	}
	LoadResult = DirectX::LoadFromTGAFile(FilePath.c_str(), &Metadata, ScratchImage);
	if (SUCCEEDED(LoadResult)) {
		return true;
	}
	LoadResult = DirectX::LoadFromHDRFile(FilePath.c_str(), &Metadata, ScratchImage);
	if (SUCCEEDED(LoadResult)) {
		return true;
	}
	ErrorMessage = "지원하지 않는 이미지 형식이거나 디코딩에 실패했습니다.";
	return false;
}

bool DroppedImageLoader::LoadAndConvertToDdsInMemory(const std::wstring& FilePath) {
	DirectX::ScratchImage DecodedImage {};
	std::string DecodeErrorMessage {};
	if (!DecodeFileToScratchImage(FilePath, DecodedImage, DecodeErrorMessage)) {
		mLastErrorMessage = DecodeErrorMessage;
		return false;
	}
	const DirectX::TexMetadata DecodedMetadata { DecodedImage.GetMetadata() };
	DirectX::Blob DdsBlob {};
	HRESULT SaveResult { DirectX::SaveToDDSMemory(DecodedImage.GetImages(), DecodedImage.GetImageCount(), DecodedMetadata, DirectX::DDS_FLAGS_NONE, DdsBlob) };
	if (FAILED(SaveResult)) {
		mLastErrorMessage = "메모리 내 DDS 변환에 실패했습니다.";
		return false;
	}
	DirectX::TexMetadata DdsMetadata {};
	DirectX::ScratchImage DdsImage {};
	HRESULT DdsLoadResult { DirectX::LoadFromDDSMemory(DdsBlob.GetBufferPointer(), DdsBlob.GetBufferSize(), DirectX::DDS_FLAGS_NONE, &DdsMetadata, DdsImage) };
	if (FAILED(DdsLoadResult)) {
		mLastErrorMessage = "메모리 내 DDS 데이터를 다시 읽지 못했습니다.";
		return false;
	}
	DirectX::ScratchImage ConvertedImage {};
	const DirectX::Image* DdsBaseImage { DdsImage.GetImage(0, 0, 0) };
	if (DdsBaseImage == nullptr) {
		mLastErrorMessage = "DDS 기본 이미지 데이터를 찾지 못했습니다.";
		return false;
	}

	// 출력 포맷 문제
	HRESULT ConvertResult { DirectX::Convert(*DdsBaseImage, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, ConvertedImage) };
	if (FAILED(ConvertResult)) {
		mLastErrorMessage = "DDS 이미지를 렌더링 가능한 포맷으로 변환하지 못했습니다.";
		mLastErrorMessage += "\n오류 코드: " + std::to_string(ConvertResult);
		return false;
	}
	mStagingResource.Reset();
	mUploadResource.Reset();
	mTextureResource.Reset();
	mWidth = static_cast<UINT>(ConvertedImage.GetMetadata().width);
	mHeight = static_cast<UINT>(ConvertedImage.GetMetadata().height);
	const DirectX::Image* ImageData { ConvertedImage.GetImage(0, 0, 0) };
	if (ImageData == nullptr) {
		mLastErrorMessage = "변환된 이미지 데이터를 찾지 못했습니다.";
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
		const SIZE_T SourceOffset { static_cast<SIZE_T>(RowIndex) * ImageData->rowPitch };
		const SIZE_T DestinationOffset { static_cast<SIZE_T>(RowIndex) * Footprint.Footprint.RowPitch };
		memcpy(DestBytes + DestinationOffset, RawPixels.data() + SourceOffset, ImageData->rowPitch);
	}
	mUploadResource->Unmap(0, nullptr);
	mCommandAllocator->Reset();
	mCommandList->Reset(mCommandAllocator.Get(), nullptr);
	D3D12_TEXTURE_COPY_LOCATION DestinationLocation {};
	DestinationLocation.pResource = mTextureResource.Get();
	DestinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	DestinationLocation.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION SourceLocation {};
	SourceLocation.pResource = mUploadResource.Get();
	SourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	SourceLocation.PlacedFootprint = Footprint;
	mCommandList->CopyTextureRegion(&DestinationLocation, 0, 0, 0, &SourceLocation, nullptr);
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
