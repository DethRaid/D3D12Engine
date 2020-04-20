#pragma once

#include <mutex>
#include <queue>

#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "../render_engine.hpp"
#include "d3d12_command_list.hpp"
#include "d3d12_descriptor_allocator.hpp"
#include "d3d12_framebuffer.hpp"
#include "resources.hpp"

namespace DirectX {
    struct XMINT2;
}

using DirectX::XMINT2;

namespace rhi {
    using Microsoft::WRL::ComPtr;

    class D3D12RenderDevice : public virtual RenderDevice {
    public:
        D3D12RenderDevice(HWND window_handle, const XMINT2& window_size);

        D3D12RenderDevice(const D3D12RenderDevice& other) = delete;
        D3D12RenderDevice& operator=(const D3D12RenderDevice& other) = delete;

        D3D12RenderDevice(D3D12RenderDevice&& old) noexcept = delete;
        D3D12RenderDevice& operator=(D3D12RenderDevice&& old) noexcept = delete;

        ~D3D12RenderDevice() override;

#pragma region RenderDevice
        [[nodiscard]] std::unique_ptr<Buffer> create_buffer(const BufferCreateInfo& create_info) override;

        [[nodiscard]] std::unique_ptr<Image> create_image(const ImageCreateInfo& create_info) override;

        [[nodiscard]] std::unique_ptr<Framebuffer> create_framebuffer(const std::vector<const Image*>& render_targets,
                                                                      const Image* depth_target) override;

        Framebuffer* get_backbuffer_framebuffer() override;

        void* map_buffer(const Buffer& buffer) override;

        void destroy_buffer(std::unique_ptr<Buffer> buffer) override;

        void destroy_image(std::unique_ptr<Image> image) override;

        void destroy_framebuffer(std::unique_ptr<Framebuffer> framebuffer) override;

        [[nodiscard]] std::unique_ptr<ComputePipelineState> create_compute_pipeline_state(
            const std::vector<uint8_t>& compute_shader) override;

        [[nodiscard]] std::unique_ptr<RenderPipelineState> create_render_pipeline_state(
            const RenderPipelineStateCreateInfo& create_info) override;

        void destroy_compute_pipeline_state(std::unique_ptr<ComputePipelineState> pipeline_state) override;

        void destroy_render_pipeline_state(std::unique_ptr<RenderPipelineState> pipeline_state) override;

        [[nodiscard]] std::unique_ptr<ResourceCommandList> create_resource_command_list() override;

        [[nodiscard]] std::unique_ptr<ComputeCommandList> create_compute_command_list() override;

        [[nodiscard]] std::unique_ptr<RenderCommandList> create_render_command_list() override;

        void submit_command_list(std::unique_ptr<CommandList> commands) override;

        void begin_frame() override;
#pragma endregion

        [[nodiscard]] bool has_separate_device_memory() const;

        [[nodiscard]] D3D12StagingBuffer get_staging_buffer(size_t num_bytes);

        void return_staging_buffer(D3D12StagingBuffer&& buffer);

        [[nodiscard]] ID3D12Device* get_d3d12_device() const;

        [[nodiscard]] UINT get_shader_resource_descriptor_size() const;

        [[nodiscard]] ComPtr<ID3D12Fence> get_next_command_list_done_fence();

    private:
        ComPtr<ID3D12Debug> debug_controller;

        ComPtr<IDXGIFactory4> factory;

        ComPtr<IDXGIAdapter> adapter;

        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12Device1> device1;

        ComPtr<ID3D12InfoQueue> info_queue;

        ComPtr<ID3D12CommandQueue> direct_command_queue;

        ComPtr<ID3D12CommandQueue> async_copy_queue;

        ComPtr<ID3D12CommandAllocator> direct_command_allocator;

        ComPtr<ID3D12CommandAllocator> compute_command_allocator;

        ComPtr<ID3D12CommandAllocator> copy_command_allocator;

        ComPtr<IDXGISwapChain3> swapchain;
        std::vector<ComPtr<ID3D12Resource>> swapchain_images;
        std::vector<D3D12Framebuffer> swapchain_framebuffers;

        ComPtr<ID3D12DescriptorHeap> cbv_srv_uav_heap;
        UINT cbv_srv_uav_size{};

        std::unique_ptr<D3D12DescriptorAllocator> rtv_allocator;

        std::unique_ptr<D3D12DescriptorAllocator> dsv_allocator;

        D3D12MA::Allocator* device_allocator;

        ComPtr<ID3D12RootSignature> standard_root_signature;

        std::vector<D3D12_INPUT_ELEMENT_DESC> standard_graphics_pipeline_input_layout;

        std::vector<D3D12StagingBuffer> staging_buffers;

        /*!
         * \brief Indicates whether this device has a Unified Memory Architecture
         *
         * UMA devices don't need to use a transfer queue to upload data, they can map a pointer directly to all resources
         */
        bool is_uma = false;

        /*!
         * \brief Indicates the level of hardware and driver support for render passes
         *
         * Tier 0 - No support, don't use renderpasses
         * Tier 1 - render targets and depth/stencil writes should use renderpasses, but UAV writes are not supported
         * Tire 2 - render targets, depth/stencil, and UAV writes should use renderpasses
         */
        D3D12_RENDER_PASS_TIER render_pass_tier = D3D12_RENDER_PASS_TIER_0;

        /*!
         * \brief Indicates support the the DXR API
         *
         * If this is `false`, the user will be unable to use any DXR shaderpacks
         */
        bool has_raytracing = false;

        DXGI_FORMAT swapchain_format{DXGI_FORMAT_R8G8B8A8_UNORM};

        std::vector<ComPtr<ID3D12Fence>> command_list_done_fences;

        std::mutex in_flight_command_lists_mutex;
        std::condition_variable commands_lists_in_flight_cv;
        std::queue<std::pair<ComPtr<ID3D12Fence>, D3D12CommandList*>> in_flight_command_lists;

        std::unique_ptr<std::thread> command_completion_thread;

        std::mutex done_command_lists_mutex;
        std::queue<D3D12CommandList*> done_command_lists;

#pragma region initialization
        void enable_validation_layer();

        void initialize_dxgi();

        void select_adapter();

        void create_queues();

        void create_swapchain(HWND window_handle, const XMINT2& window_size, UINT num_images);

        void create_command_allocators();

        void create_descriptor_heaps();

        void initialize_swapchain_descriptors();

        [[nodiscard]] std::pair<ComPtr<ID3D12DescriptorHeap>, UINT> create_descriptor_allocator(D3D12_DESCRIPTOR_HEAP_TYPE descriptor_type,
                                                                                                uint32_t num_descriptors) const;

        void initialize_dma();

        void create_standard_root_signature();

        [[nodiscard]] ComPtr<ID3D12RootSignature> compile_root_signature(const D3D12_ROOT_SIGNATURE_DESC& root_signature_desc) const;

        void create_material_resource_binder();

        void create_standard_graphics_pipeline_input_layout();
#pragma endregion

        [[nodiscard]] D3D12StagingBuffer create_staging_buffer(size_t num_bytes) const;

        static void wait_for_command_lists(D3D12RenderDevice* render_device);
    };
} // namespace render
