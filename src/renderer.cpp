#include "renderer.hpp"

#define D3DX12_NO_STATE_OBJECT_HELPERS
#define D3DX12_NO_CHECK_FEATURE_SUPPORT_CLASS
#include "d3dx12.h"

#include <d3dcompiler.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace meshviewer
{
namespace
{

using Microsoft::WRL::ComPtr;

struct FrameConstants
{
    DirectX::XMFLOAT4X4 mvp;
    DirectX::XMFLOAT4X4 world;
    DirectX::XMFLOAT4 lightDirection;
};

static_assert(offsetof(FrameConstants, mvp) == 0);
static_assert(offsetof(FrameConstants, world) == 64);
static_assert(offsetof(FrameConstants, lightDirection) == 128);
static_assert(sizeof(FrameConstants) == 144);

ComPtr<ID3DBlob> CompileShader(const std::filesystem::path &path, const char *entry, const char *target)
{
    if (!std::filesystem::is_regular_file(path))
    {
        throw std::runtime_error("shader file not found: " + path.string());
    }

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> shader;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
                                          flags, 0, &shader, &errors);
    if (FAILED(hr))
    {
        std::string message = std::string("HLSL compile failed (") + entry + "): ";
        if (errors && errors->GetBufferPointer())
        {
            message += static_cast<const char *>(errors->GetBufferPointer());
        }
        else
        {
            message += HResultText(hr, "D3DCompileFromFile");
        }
        throw std::runtime_error(message);
    }
    return shader;
}

ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device *device, D3D12_HEAP_TYPE heapType, UINT64 size,
                                    D3D12_RESOURCE_STATES state)
{
    const CD3DX12_HEAP_PROPERTIES heap(heapType);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)),
        "CreateCommittedResource");
    return resource;
}

void FillUpload(ID3D12Resource *upload, const void *data, UINT64 size)
{
    void *mapped = nullptr;
    ThrowIfFailed(upload->Map(0, nullptr, &mapped), "Map");
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    upload->Unmap(0, nullptr);
}

ComPtr<IDXGIAdapter1> SelectAdapter(IDXGIFactory4 *factory)
{
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0;; ++index)
    {
        if (factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf()) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
        {
            OutputDebugStringW(L"Mesh viewer adapter: ");
            OutputDebugStringW(desc.Description);
            OutputDebugStringW(L"\n");
            return adapter;
        }
    }

    ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
    OutputDebugStringW(L"Mesh viewer adapter: WARP\n");
    return adapter;
}

} // namespace

Renderer::~Renderer()
{
    try
    {
        WaitForGpu();
    }
    catch (const std::exception &)
    {
    }
    if (constantBuffer_ && constantMapped_)
    {
        constantBuffer_->Unmap(0, nullptr);
        constantMapped_ = nullptr;
    }
    if (fenceEvent_)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void Renderer::Initialize(HWND hwnd, std::uint32_t width, std::uint32_t height, const std::filesystem::path &shaderPath)
{
    if (width == 0 || height == 0)
    {
        throw std::runtime_error("window client size is empty");
    }
    width_ = width;
    height_ = height;

    // Factory, adapter enumeration, and the device come first. IDXGIFactory4 is the
    // IDXGIFactory used to enumerate adapters and create the swap chain.
    CreateDevice();
    CreateCommands();
    CreateSwapChain(hwnd);
    CreateDescriptorHeaps();
    CreateSizeDependentResources();
    CreateRootSignature();
    CreatePipeline(shaderPath);
    CreateConstantBuffer();
}

void Renderer::CreateDevice()
{
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    HRESULT factoryHr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(factoryHr) && factoryFlags != 0)
    {
        factory_.Reset();
        factoryHr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
    }
    ThrowIfFailed(factoryHr, "CreateDXGIFactory2");

    const ComPtr<IDXGIAdapter1> adapter = SelectAdapter(factory_.Get());
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)),
                  "D3D12CreateDevice");
}

void Renderer::CreateCommands()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_)), "CreateCommandQueue");
    ThrowIfFailed(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)),
                  "CreateCommandAllocator");
    ThrowIfFailed(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                             IID_PPV_ARGS(&commandList_)),
                  "CreateCommandList");
    ThrowIfFailed(commandList_->Close(), "Close");

    ThrowIfFailed(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_)
    {
        throw std::runtime_error("CreateEvent failed");
    }
}

void Renderer::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kFrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory_->CreateSwapChainForHwnd(queue_.Get(), hwnd, &desc, nullptr, nullptr, &swapChain),
                  "CreateSwapChainForHwnd");
    ThrowIfFailed(factory_->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
    ThrowIfFailed(swapChain.As(&swapChain_), "QueryInterface(IDXGISwapChain3)");
}

void Renderer::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kFrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)), "CreateDescriptorHeap(RTV)");
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)), "CreateDescriptorHeap(DSV)");
}

void Renderer::CreateSizeDependentResources()
{
    CreateRenderTargets();
    CreateDepthBuffer();
    UpdateViewport();
}

void Renderer::CreateRenderTargets()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(rtvHeap_->GetCPUDescriptorHandleForHeapStart());
    for (UINT index = 0; index < kFrameCount; ++index)
    {
        ThrowIfFailed(swapChain_->GetBuffer(index, IID_PPV_ARGS(&renderTargets_[index])), "GetBuffer");
        device_->CreateRenderTargetView(renderTargets_[index].Get(), nullptr, rtv);
        rtv.Offset(1, rtvDescriptorSize_);
    }
}

void Renderer::CreateDepthBuffer()
{
    const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width_, height_, 1, 1, 1, 0,
                                                   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                   &clear, IID_PPV_ARGS(&depth_)),
                  "CreateCommittedResource(depth)");
    device_->CreateDepthStencilView(depth_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}

void Renderer::UpdateViewport()
{
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.Width = static_cast<float>(width_);
    viewport_.Height = static_cast<float>(height_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
    scissor_.left = 0;
    scissor_.top = 0;
    scissor_.right = static_cast<LONG>(width_);
    scissor_.bottom = static_cast<LONG>(height_);
}

void Renderer::CreateRootSignature()
{
    CD3DX12_ROOT_PARAMETER parameter;
    parameter.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC desc;
    desc.Init(1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    const HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
    if (FAILED(hr))
    {
        std::string message = "D3D12SerializeRootSignature failed";
        if (error && error->GetBufferPointer())
        {
            message += ": ";
            message += static_cast<const char *>(error->GetBufferPointer());
        }
        throw std::runtime_error(message);
    }
    ThrowIfFailed(device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                               IID_PPV_ARGS(&rootSignature_)),
                  "CreateRootSignature");
}

void Renderer::CreatePipeline(const std::filesystem::path &shaderPath)
{
    const ComPtr<ID3DBlob> vertexShader = CompileShader(shaderPath, "VSMain", "vs_5_0");
    const ComPtr<ID3DBlob> pixelShader = CompileShader(shaderPath, "PSMain", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(CpuVertex, position)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(CpuVertex, normal)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(CpuVertex, uv)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(CpuVertex, color)),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = rootSignature_.Get();
    pso.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    pso.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.InputLayout = {layout, 4};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    ThrowIfFailed(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline_)), "CreateGraphicsPipelineState");
}

void Renderer::CreateConstantBuffer()
{
    constantBuffer_ =
        CreateBuffer(device_.Get(), D3D12_HEAP_TYPE_UPLOAD, kConstantBytes, D3D12_RESOURCE_STATE_GENERIC_READ);
    const D3D12_RANGE readRange{0, 0};
    ThrowIfFailed(constantBuffer_->Map(0, &readRange, reinterpret_cast<void **>(&constantMapped_)), "Map");
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (!swapChain_ || width == 0 || height == 0 || (width == width_ && height == height_))
    {
        return;
    }
    WaitForGpu();
    for (UINT index = 0; index < kFrameCount; ++index)
    {
        renderTargets_[index].Reset();
    }
    depth_.Reset();
    ThrowIfFailed(swapChain_->ResizeBuffers(kFrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                  "ResizeBuffers");
    width_ = width;
    height_ = height;
    CreateSizeDependentResources();
}

void Renderer::UploadMesh(const CpuMesh &mesh)
{
    if (!device_ || !commandList_)
    {
        throw std::runtime_error("renderer is not initialized");
    }
    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        throw std::runtime_error("mesh has no triangles");
    }

    const UINT64 vertexBytes = static_cast<UINT64>(mesh.vertices.size() * sizeof(CpuVertex));
    const UINT64 indexBytes = static_cast<UINT64>(mesh.indices.size() * sizeof(std::uint32_t));
    if (vertexBytes > std::numeric_limits<UINT>::max() || indexBytes > std::numeric_limits<UINT>::max())
    {
        throw std::runtime_error("mesh is too large for a Direct3D 12 buffer view");
    }

    ComPtr<ID3D12Resource> vertexBuffer =
        CreateBuffer(device_.Get(), D3D12_HEAP_TYPE_DEFAULT, vertexBytes, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12Resource> indexBuffer =
        CreateBuffer(device_.Get(), D3D12_HEAP_TYPE_DEFAULT, indexBytes, D3D12_RESOURCE_STATE_COPY_DEST);
    ComPtr<ID3D12Resource> vertexUpload =
        CreateBuffer(device_.Get(), D3D12_HEAP_TYPE_UPLOAD, vertexBytes, D3D12_RESOURCE_STATE_GENERIC_READ);
    ComPtr<ID3D12Resource> indexUpload =
        CreateBuffer(device_.Get(), D3D12_HEAP_TYPE_UPLOAD, indexBytes, D3D12_RESOURCE_STATE_GENERIC_READ);
    FillUpload(vertexUpload.Get(), mesh.vertices.data(), vertexBytes);
    FillUpload(indexUpload.Get(), mesh.indices.data(), indexBytes);

    WaitForGpu();
    ThrowIfFailed(allocator_->Reset(), "Reset(allocator)");
    ThrowIfFailed(commandList_->Reset(allocator_.Get(), nullptr), "Reset(command list)");
    commandList_->CopyBufferRegion(vertexBuffer.Get(), 0, vertexUpload.Get(), 0, vertexBytes);
    commandList_->CopyBufferRegion(indexBuffer.Get(), 0, indexUpload.Get(), 0, indexBytes);
    const D3D12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        CD3DX12_RESOURCE_BARRIER::Transition(indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_INDEX_BUFFER),
    };
    commandList_->ResourceBarrier(2, barriers);
    ThrowIfFailed(commandList_->Close(), "Close");

    ID3D12CommandList *lists[] = {commandList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    Signal();
    WaitForGpu();

    vertexBuffer_ = std::move(vertexBuffer);
    indexBuffer_ = std::move(indexBuffer);
    vertexBuffer_->SetName(L"MeshVertexBuffer");
    indexBuffer_->SetName(L"MeshIndexBuffer");

    vertexView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexView_.SizeInBytes = static_cast<UINT>(vertexBytes);
    vertexView_.StrideInBytes = sizeof(CpuVertex);
    indexView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    indexView_.SizeInBytes = static_cast<UINT>(indexBytes);
    indexView_.Format = DXGI_FORMAT_R32_UINT;
    indexCount_ = static_cast<UINT>(mesh.indices.size());
}

void Renderer::Render(const DirectX::XMMATRIX &world, const DirectX::XMMATRIX &view,
                      const DirectX::XMMATRIX &projection)
{
    if (!swapChain_ || width_ == 0 || height_ == 0)
    {
        return;
    }

    WaitForGpu();
    ThrowIfFailed(allocator_->Reset(), "Reset(allocator)");
    ThrowIfFailed(commandList_->Reset(allocator_.Get(), pipeline_.Get()), "Reset(command list)");

    FrameConstants constants{};
    const DirectX::XMMATRIX mvp = world * view * projection;
    DirectX::XMStoreFloat4x4(&constants.mvp, DirectX::XMMatrixTranspose(mvp));
    DirectX::XMStoreFloat4x4(&constants.world, DirectX::XMMatrixTranspose(world));
    DirectX::XMStoreFloat4(&constants.lightDirection,
                           DirectX::XMVector3Normalize(DirectX::XMVectorSet(0.35f, 0.85f, 0.40f, 0.0f)));
    std::memcpy(constantMapped_, &constants, sizeof(constants));

    const UINT frame = swapChain_->GetCurrentBackBufferIndex();
    const auto toRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets_[frame].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList_->ResourceBarrier(1, &toRenderTarget);

    const CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(rtvHeap_->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(frame),
                                            rtvDescriptorSize_);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const float clearColor[] = {0.11f, 0.12f, 0.14f, 1.0f};
    commandList_->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissor_);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->SetPipelineState(pipeline_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->SetGraphicsRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress());

    if (indexCount_ > 0)
    {
        commandList_->IASetVertexBuffers(0, 1, &vertexView_);
        commandList_->IASetIndexBuffer(&indexView_);
        commandList_->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
    }

    const auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets_[frame].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList_->ResourceBarrier(1, &toPresent);
    ThrowIfFailed(commandList_->Close(), "Close");

    ID3D12CommandList *lists[] = {commandList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const HRESULT presentHr = swapChain_->Present(1, 0);
    Signal();
    if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET)
    {
        throw std::runtime_error(HResultText(device_->GetDeviceRemovedReason(), "D3D12 device removed"));
    }
    ThrowIfFailed(presentHr, "Present");
}

void Renderer::Signal()
{
    const UINT64 value = fenceValue_ + 1;
    ThrowIfFailed(queue_->Signal(fence_.Get(), value), "Signal");
    fenceValue_ = value;
}

void Renderer::WaitForGpu()
{
    if (!queue_ || !fence_ || !fenceEvent_ || fenceValue_ == 0)
    {
        return;
    }
    if (fence_->GetCompletedValue() < fenceValue_)
    {
        ThrowIfFailed(fence_->SetEventOnCompletion(fenceValue_, fenceEvent_), "SetEventOnCompletion");
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

} // namespace meshviewer
