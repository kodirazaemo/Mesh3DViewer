#pragma once

#include "dx_util.hpp"
#include "mesh_loader.hpp"

#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>

namespace meshviewer
{

class Renderer
{
  public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    void Initialize(HWND hwnd, std::uint32_t width, std::uint32_t height, const std::filesystem::path &shaderPath);
    void Resize(std::uint32_t width, std::uint32_t height);
    void UploadMesh(const CpuMesh &mesh);
    void UnloadMesh();
    [[nodiscard]] bool HasMesh() const;
    void Render(const DirectX::XMMATRIX &world, const DirectX::XMMATRIX &view, const DirectX::XMMATRIX &projection);
    void WaitForGpu();

  private:
    void CreateDevice();
    void CreateCommands();
    void CreateSwapChain(HWND hwnd);
    void CreateDescriptorHeaps();
    void CreateSizeDependentResources();
    void CreateRenderTargets();
    void CreateDepthBuffer();
    void UpdateViewport();
    void CreateRootSignature();
    void CreatePipeline(const std::filesystem::path &shaderPath);
    void CreateConstantBuffer();
    void UploadTexture(const CpuMesh &mesh);
    void Signal();

    static constexpr UINT kFrameCount = 2;
    static constexpr UINT kConstantBytes = 256;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> renderTargets_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
    Microsoft::WRL::ComPtr<ID3D12Resource> textureUpload_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    std::uint8_t *constantMapped_ = nullptr;
    HANDLE fenceEvent_ = nullptr;
    bool comInitialized_ = false;
    UINT64 fenceValue_ = 0;
    UINT rtvDescriptorSize_ = 0;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT indexCount_ = 0;
    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissor_{};
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    D3D12_INDEX_BUFFER_VIEW indexView_{};
};

} // namespace meshviewer
