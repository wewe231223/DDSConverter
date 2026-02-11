#include "framework.h"
#include "DroppedImageLoader.h"

#include <cstring>
#include <vector>

#include "DirectXTex.h"

DroppedImageLoader::DroppedImageLoader()
	: mDevice{ nullptr }
	, mCommandQueue{ nullptr }
	, mSrvHeap{ nullptr }
	, mSrvDescriptorSize{ 0 }
	, mSourceDescriptorIndex{ 0 }
	, mConvertedDescriptorIndex{ 0 }
	, mFenceValue{ 0 }
	, mFenceEvent{ nullptr }
	, mInitialized{ false }
	, mHasImage{ false }
	, mSourceWidth{ 0 }
	, mSourceHeight{ 0 }
	, mConvertedWidth{ 0 }
	, mConvertedHeight{ 0 }
	, mFilePath{}
	, mLastErrorMessage{}
	, mSourceTextureId{ nullptr }
	, mConvertedTextureId{ nullptr }
	, mOriginalScratchImage{} {
}

DroppedImageLoader::~DroppedImageLoader() {
	Shutdown();
}

bool DroppedImageLoader::Initialize(ID3D12Device* Device, ID3D12CommandQueue* CommandQueue, ID3D12DescriptorHeap* SrvHeap, UINT SrvDescriptorSize, UINT SourceDescriptorIndex, UINT ConvertedDescriptorIndex) {
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
	mSourceDescriptorIndex = SourceDescriptorIndex;
	mConvertedDescriptorIndex = ConvertedDescriptorIndex;
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
	mSourceTextureResource.Reset();
	mSourceUploadResource.Reset();
	mConvertedTextureResource.Reset();
	mConvertedUploadResource.Reset();
	mFence.Reset();
	mCommandList.Reset();
	mCommandAllocator.Reset();
	if (mFenceEvent != nullptr) {
		CloseHandle(mFenceEvent);
		mFenceEvent = nullptr;
	}
	mOriginalScratchImage.reset();
	mSourceTextureId = nullptr;
	mConvertedTextureId = nullptr;
	mFilePath.clear();
	mLastErrorMessage.clear();
	mSourceWidth = 0;
	mSourceHeight = 0;
	mConvertedWidth = 0;
	mConvertedHeight = 0;
	mHasImage = false;
	mInitialized = false;
	mFenceValue = 0;
	mSourceDescriptorIndex = 0;
	mConvertedDescriptorIndex = 0;
	mSrvDescriptorSize = 0;
	mSrvHeap = nullptr;
	mCommandQueue = nullptr;
	mDevice = nullptr;
}

bool DroppedImageLoader::LoadImageFile(const std::wstring& FilePath, const DdsConversionOptions& ConversionOptions) {
	if (!mInitialized) {
		mLastErrorMessage = "이미지 로더가 초기화되지 않았습니다.";
		return false;
	}
	std::unique_ptr<DirectX::ScratchImage> DecodedImage{ std::make_unique<DirectX::ScratchImage>() };
	std::string DecodeErrorMessage{};
	if (!DecodeFileToScratchImage(FilePath, *DecodedImage, DecodeErrorMessage)) {
		mLastErrorMessage = DecodeErrorMessage;
		mHasImage = false;
		return false;
	}
	mOriginalScratchImage = std::move(DecodedImage);
	mFilePath = FilePath;
	std::string UploadErrorMessage{};
	if (!UploadTextureToDescriptor(*mOriginalScratchImage, mSourceDescriptorIndex, mSourceTextureResource, mSourceUploadResource, mSourceTextureId, mSourceWidth, mSourceHeight, UploadErrorMessage)) {
		mLastErrorMessage = UploadErrorMessage;
		mHasImage = false;
		return false;
	}
	if (!RebuildConvertedImage(ConversionOptions)) {
		mHasImage = false;
		return false;
	}
	mHasImage = true;
	mLastErrorMessage.clear();
	return true;
}

bool DroppedImageLoader::RebuildConvertedImage(const DdsConversionOptions& ConversionOptions) {
	if (!mInitialized) {
		mLastErrorMessage = "이미지 로더가 초기화되지 않았습니다.";
		return false;
	}
	if (mOriginalScratchImage == nullptr) {
		mLastErrorMessage = "원본 이미지가 없어 DDS 변환을 수행할 수 없습니다.";
		return false;
	}
	DirectX::ScratchImage ConvertedScratchImage{};
	std::string BuildErrorMessage{};
	if (!BuildConvertedScratchImage(ConversionOptions, ConvertedScratchImage, BuildErrorMessage)) {
		mLastErrorMessage = BuildErrorMessage;
		return false;
	}
	std::string UploadErrorMessage{};
	if (!UploadTextureToDescriptor(ConvertedScratchImage, mConvertedDescriptorIndex, mConvertedTextureResource, mConvertedUploadResource, mConvertedTextureId, mConvertedWidth, mConvertedHeight, UploadErrorMessage)) {
		mLastErrorMessage = UploadErrorMessage;
		return false;
	}
	mLastErrorMessage.clear();
	return true;
}

bool DroppedImageLoader::HasImage() const {
	return mHasImage;
}

ImTextureID DroppedImageLoader::GetSourceTextureId() const {
	return mSourceTextureId;
}

ImTextureID DroppedImageLoader::GetConvertedTextureId() const {
	return mConvertedTextureId;
}

UINT DroppedImageLoader::GetSourceWidth() const {
	return mSourceWidth;
}

UINT DroppedImageLoader::GetSourceHeight() const {
	return mSourceHeight;
}

UINT DroppedImageLoader::GetConvertedWidth() const {
	return mConvertedWidth;
}

UINT DroppedImageLoader::GetConvertedHeight() const {
	return mConvertedHeight;
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
	const UINT64 FenceToWaitFor{ mFenceValue };
	mCommandQueue->Signal(mFence.Get(), FenceToWaitFor);
	mFenceValue += 1;
	if (mFence->GetCompletedValue() < FenceToWaitFor) {
		mFence->SetEventOnCompletion(FenceToWaitFor, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);
	}
}

bool DroppedImageLoader::DecodeFileToScratchImage(const std::wstring& FilePath, DirectX::ScratchImage& ScratchImage, std::string& ErrorMessage) const {
	DirectX::TexMetadata Metadata{};
	HRESULT LoadResult{ DirectX::LoadFromWICFile(FilePath.c_str(), DirectX::WIC_FLAGS_NONE, &Metadata, ScratchImage) };
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

bool DroppedImageLoader::BuildConvertedScratchImage(const DdsConversionOptions& ConversionOptions, DirectX::ScratchImage& ConvertedScratchImage, std::string& ErrorMessage) const {
	const DirectX::TexMetadata OriginalMetadata{ mOriginalScratchImage->GetMetadata() };
	DirectX::ScratchImage LinearImage{};
	DXGI_FORMAT ConvertTargetFormat{ ConversionOptions.OutputFormat == DdsOutputFormat::Rgba8UnormSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM };
	const DirectX::Image* OriginalBaseImage{ mOriginalScratchImage->GetImage(0, 0, 0) };
	if (OriginalBaseImage == nullptr) {
		ErrorMessage = "원본 이미지 데이터를 찾지 못했습니다.";
		return false;
	}
	HRESULT ConvertResult{ DirectX::Convert(*OriginalBaseImage, ConvertTargetFormat, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, LinearImage) };
	if (FAILED(ConvertResult)) {
		ErrorMessage = "원본 이미지를 변환 가능한 포맷으로 변환하지 못했습니다.";
		return false;
	}
	DirectX::ScratchImage SourceForCompression{};
	DirectX::TexMetadata SourceMetadata{ LinearImage.GetMetadata() };
	if (ConversionOptions.GenerateMipMaps) {
		HRESULT MipResult{ DirectX::GenerateMipMaps(LinearImage.GetImages(), LinearImage.GetImageCount(), SourceMetadata, DirectX::TEX_FILTER_DEFAULT, 0, SourceForCompression) };
		if (FAILED(MipResult)) {
			ErrorMessage = "밉맵 생성에 실패했습니다.";
			return false;
		}
		SourceMetadata = SourceForCompression.GetMetadata();
	}
	else {
		HRESULT InitializeResult{ SourceForCompression.InitializeFromImage(*LinearImage.GetImage(0, 0, 0), false) };
		if (FAILED(InitializeResult)) {
			ErrorMessage = "원본 이미지 초기화에 실패했습니다.";
			return false;
		}
		SourceMetadata = SourceForCompression.GetMetadata();
	}
	const DXGI_FORMAT TargetFormat{ GetDxgiFormat(ConversionOptions.OutputFormat) };
	const bool IsBlockCompressed{ DirectX::IsCompressed(TargetFormat) };
	DirectX::ScratchImage DdsReadyImage{};
	if (IsBlockCompressed) {
		DirectX::TEX_COMPRESS_FLAGS CompressFlags{ static_cast<DirectX::TEX_COMPRESS_FLAGS>(GetCompressFlags(ConversionOptions)) };
		DirectX::XMVECTOR CompressionWeight{ DirectX::XMVectorSet(ConversionOptions.CompressionWeightRed, ConversionOptions.CompressionWeightGreen, ConversionOptions.CompressionWeightBlue, 1.0F) };
		HRESULT CompressResult{ DirectX::Compress(SourceForCompression.GetImages(), SourceForCompression.GetImageCount(), SourceMetadata, TargetFormat, CompressFlags, ConversionOptions.AlphaReference, DdsReadyImage, CompressionWeight) };
		if (FAILED(CompressResult)) {
			ErrorMessage = "DDS 압축 변환에 실패했습니다.";
			return false;
		}
	}
	else {
		HRESULT CopyResult{ DirectX::Convert(SourceForCompression.GetImages(), SourceForCompression.GetImageCount(), SourceMetadata, TargetFormat, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, DdsReadyImage) };
		if (FAILED(CopyResult)) {
			ErrorMessage = "DDS 비압축 변환에 실패했습니다.";
			return false;
		}
	}
	DirectX::Blob DdsBlob{};
	HRESULT SaveResult{ DirectX::SaveToDDSMemory(DdsReadyImage.GetImages(), DdsReadyImage.GetImageCount(), DdsReadyImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, DdsBlob) };
	if (FAILED(SaveResult)) {
		ErrorMessage = "메모리 내 DDS 저장에 실패했습니다.";
		return false;
	}
	DirectX::ScratchImage ReloadedDdsImage{};
	DirectX::TexMetadata ReloadedMetadata{};
	HRESULT ReloadResult{ DirectX::LoadFromDDSMemory(DdsBlob.GetBufferPointer(), DdsBlob.GetBufferSize(), DirectX::DDS_FLAGS_NONE, &ReloadedMetadata, ReloadedDdsImage) };
	if (FAILED(ReloadResult)) {
		ErrorMessage = "변환된 DDS 재로딩에 실패했습니다.";
		return false;
	}
	const DirectX::Image* ReloadedBaseImage{ ReloadedDdsImage.GetImage(0, 0, 0) };
	if (ReloadedBaseImage == nullptr) {
		ErrorMessage = "변환된 DDS 이미지 데이터를 찾지 못했습니다.";
		return false;
	}
	DXGI_FORMAT DisplayFormat{ ConversionOptions.OutputFormat == DdsOutputFormat::Rgba8UnormSrgb || ConversionOptions.OutputFormat == DdsOutputFormat::Bc1UnormSrgb || ConversionOptions.OutputFormat == DdsOutputFormat::Bc2UnormSrgb || ConversionOptions.OutputFormat == DdsOutputFormat::Bc3UnormSrgb || ConversionOptions.OutputFormat == DdsOutputFormat::Bc7UnormSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM };
	HRESULT DisplayConvertResult{ DirectX::Convert(*ReloadedBaseImage, DisplayFormat, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, ConvertedScratchImage) };
	if (FAILED(DisplayConvertResult)) {
		ErrorMessage = "DDS 이미지를 렌더링 가능한 포맷으로 변환하지 못했습니다.";
		return false;
	}
	UNREFERENCED_PARAMETER(OriginalMetadata);
	return true;
}

bool DroppedImageLoader::UploadTextureToDescriptor(const DirectX::ScratchImage& ScratchImage, UINT DescriptorIndex, Microsoft::WRL::ComPtr<ID3D12Resource>& TextureResource, Microsoft::WRL::ComPtr<ID3D12Resource>& UploadResource, ImTextureID& TextureId, UINT& Width, UINT& Height, std::string& ErrorMessage) {
	const DirectX::Image* ImageData{ ScratchImage.GetImage(0, 0, 0) };
	if (ImageData == nullptr) {
		ErrorMessage = "업로드할 이미지 데이터를 찾지 못했습니다.";
		return false;
	}
	TextureResource.Reset();
	UploadResource.Reset();
	D3D12_RESOURCE_DESC TextureDesc{};
	TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	TextureDesc.Alignment = 0;
	TextureDesc.Width = ImageData->width;
	TextureDesc.Height = static_cast<UINT>(ImageData->height);
	TextureDesc.DepthOrArraySize = 1;
	TextureDesc.MipLevels = 1;
	TextureDesc.Format = ImageData->format;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	TextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_HEAP_PROPERTIES TextureHeapProperties{};
	TextureHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	TextureHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	TextureHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	TextureHeapProperties.CreationNodeMask = 1;
	TextureHeapProperties.VisibleNodeMask = 1;
	HRESULT TextureCreateResult{ mDevice->CreateCommittedResource(&TextureHeapProperties, D3D12_HEAP_FLAG_NONE, &TextureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&TextureResource)) };
	if (FAILED(TextureCreateResult)) {
		ErrorMessage = "텍스처 리소스 생성에 실패했습니다.";
		return false;
	}
	UINT64 UploadBufferSize{ 0 };
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint{};
	UINT NumRows{ 0 };
	UINT64 RowSizeInBytes{ 0 };
	mDevice->GetCopyableFootprints(&TextureDesc, 0, 1, 0, &Footprint, &NumRows, &RowSizeInBytes, &UploadBufferSize);
	D3D12_RESOURCE_DESC UploadDesc{};
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
	D3D12_HEAP_PROPERTIES UploadHeapProperties{};
	UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	UploadHeapProperties.CreationNodeMask = 1;
	UploadHeapProperties.VisibleNodeMask = 1;
	HRESULT UploadCreateResult{ mDevice->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &UploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&UploadResource)) };
	if (FAILED(UploadCreateResult)) {
		ErrorMessage = "업로드 버퍼 생성에 실패했습니다.";
		return false;
	}
	void* MappedData{ nullptr };
	D3D12_RANGE ReadRange{};
	ReadRange.Begin = 0;
	ReadRange.End = 0;
	HRESULT MapResult{ UploadResource->Map(0, &ReadRange, &MappedData) };
	if (FAILED(MapResult)) {
		ErrorMessage = "업로드 버퍼 맵핑에 실패했습니다.";
		return false;
	}
	unsigned char* DestinationBytes{ static_cast<unsigned char*>(MappedData) };
	for (UINT RowIndex{ 0 }; RowIndex < NumRows; ++RowIndex) {
		const SIZE_T SourceOffset{ static_cast<SIZE_T>(RowIndex) * ImageData->rowPitch };
		const SIZE_T DestinationOffset{ static_cast<SIZE_T>(RowIndex) * Footprint.Footprint.RowPitch };
		memcpy(DestinationBytes + DestinationOffset, ImageData->pixels + SourceOffset, ImageData->rowPitch);
	}
	UploadResource->Unmap(0, nullptr);
	mCommandAllocator->Reset();
	mCommandList->Reset(mCommandAllocator.Get(), nullptr);
	D3D12_TEXTURE_COPY_LOCATION DestinationLocation{};
	DestinationLocation.pResource = TextureResource.Get();
	DestinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	DestinationLocation.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION SourceLocation{};
	SourceLocation.pResource = UploadResource.Get();
	SourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	SourceLocation.PlacedFootprint = Footprint;
	mCommandList->CopyTextureRegion(&DestinationLocation, 0, 0, 0, &SourceLocation, nullptr);
	D3D12_RESOURCE_BARRIER Barrier{};
	Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	Barrier.Transition.pResource = TextureResource.Get();
	Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	mCommandList->ResourceBarrier(1, &Barrier);
	mCommandList->Close();
	ID3D12CommandList* CommandLists[]{ mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, CommandLists);
	WaitForGpu();
	D3D12_SHADER_RESOURCE_VIEW_DESC ShaderResourceViewDesc{};
	ShaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	ShaderResourceViewDesc.Format = TextureDesc.Format;
	ShaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	ShaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
	ShaderResourceViewDesc.Texture2D.MipLevels = 1;
	ShaderResourceViewDesc.Texture2D.PlaneSlice = 0;
	ShaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0F;
	D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle{ GetCpuHandle(DescriptorIndex) };
	mDevice->CreateShaderResourceView(TextureResource.Get(), &ShaderResourceViewDesc, CpuHandle);
	D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle{ GetGpuHandle(DescriptorIndex) };
	TextureId = reinterpret_cast<ImTextureID>(GpuHandle.ptr);
	Width = static_cast<UINT>(ImageData->width);
	Height = static_cast<UINT>(ImageData->height);
	ErrorMessage.clear();
	return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE DroppedImageLoader::GetCpuHandle(UINT DescriptorIndex) const {
	D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle{ mSrvHeap->GetCPUDescriptorHandleForHeapStart() };
	CpuHandle.ptr += static_cast<SIZE_T>(DescriptorIndex) * static_cast<SIZE_T>(mSrvDescriptorSize);
	return CpuHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DroppedImageLoader::GetGpuHandle(UINT DescriptorIndex) const {
	D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle{ mSrvHeap->GetGPUDescriptorHandleForHeapStart() };
	GpuHandle.ptr += static_cast<UINT64>(DescriptorIndex) * static_cast<UINT64>(mSrvDescriptorSize);
	return GpuHandle;
}

DXGI_FORMAT DroppedImageLoader::GetDxgiFormat(DdsOutputFormat OutputFormat) const {
	switch (OutputFormat) {
	case DdsOutputFormat::Bc1Unorm:
		return DXGI_FORMAT_BC1_UNORM;
	case DdsOutputFormat::Bc1UnormSrgb:
		return DXGI_FORMAT_BC1_UNORM_SRGB;
	case DdsOutputFormat::Bc2Unorm:
		return DXGI_FORMAT_BC2_UNORM;
	case DdsOutputFormat::Bc2UnormSrgb:
		return DXGI_FORMAT_BC2_UNORM_SRGB;
	case DdsOutputFormat::Bc3Unorm:
		return DXGI_FORMAT_BC3_UNORM;
	case DdsOutputFormat::Bc3UnormSrgb:
		return DXGI_FORMAT_BC3_UNORM_SRGB;
	case DdsOutputFormat::Bc4Unorm:
		return DXGI_FORMAT_BC4_UNORM;
	case DdsOutputFormat::Bc4Snorm:
		return DXGI_FORMAT_BC4_SNORM;
	case DdsOutputFormat::Bc5Unorm:
		return DXGI_FORMAT_BC5_UNORM;
	case DdsOutputFormat::Bc5Snorm:
		return DXGI_FORMAT_BC5_SNORM;
	case DdsOutputFormat::Bc6hUf16:
		return DXGI_FORMAT_BC6H_UF16;
	case DdsOutputFormat::Bc6hSf16:
		return DXGI_FORMAT_BC6H_SF16;
	case DdsOutputFormat::Bc7Unorm:
		return DXGI_FORMAT_BC7_UNORM;
	case DdsOutputFormat::Bc7UnormSrgb:
		return DXGI_FORMAT_BC7_UNORM_SRGB;
	case DdsOutputFormat::Rgba8Unorm:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DdsOutputFormat::Rgba8UnormSrgb:
		return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	default:
		return DXGI_FORMAT_BC7_UNORM;
	}
}

DWORD DroppedImageLoader::GetCompressFlags(const DdsConversionOptions& ConversionOptions) const {
	DWORD CompressFlags{ DirectX::TEX_COMPRESS_DEFAULT };
	if (ConversionOptions.EnableDithering) {
		CompressFlags |= DirectX::TEX_COMPRESS_DITHER;
	}
	if (ConversionOptions.UseUniformWeighting) {
		CompressFlags |= DirectX::TEX_COMPRESS_UNIFORM;
	}
	return CompressFlags;
}
