#include "render_backend.hpp"

#define GLFW_EXPOSE_NATIVE_WIN32

#include <algorithm>

#include <combaseapi.h>
#include <d3dcompiler.h>
#include <dxgi1_3.h>

#include "D3D12MemAlloc.h"
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"
#include "TracyD3D12.hpp"
#include "adapters/rex/rex_wrapper.hpp"
#include "adapters/tracy.hpp"
#include "core/constants.hpp"
#include "pix3.h"
#include "renderer/rhi/d3d12_private_data.hpp"
#include "renderer/rhi/d3dx12.hpp"
#include "renderer/rhi/helpers.hpp"
#include "renderer/rhi/render_pipeline_state.hpp"
#include "rx/core/abort.h"
#include "rx/core/log.h"
#include "rx/core/string.h"
#include "windows/windows_helpers.hpp"

namespace sanity::engine::renderer {
    RX_LOG("\033[32mRenderDevice\033[0m", logger);

    RX_CONSOLE_BVAR(cvar_enable_debug_layers, "r.EnableDebugLayers", "Enable the D3D12 and DXGI debug layers", true);

    RX_CONSOLE_BVAR(
        cvar_enable_gpu_based_validation,
        "r.EnableGpuBasedValidation",
        "Enables in-depth validation of operations on the GPU. This has a significant performance cost and should be used sparingly",
        false);

    RX_CONSOLE_IVAR(
        cvar_max_in_flight_gpu_frames, "r.MaxInFlightGpuFrames", "Maximum number of frames that the GPU may work on concurrently", 1, 8, 3);

    RX_CONSOLE_BVAR(cvar_break_on_validation_error,
                    "r.BreakOnValidationError",
                    "Whether to issue a breakpoint when the validation layer encounters an error",
                    true);

    RX_CONSOLE_BVAR(cvar_verify_every_command_list_submission,
                    "r.VerifyEveryCommandListSubmission",
                    "If enabled, SanityEngine will wait for every command list to check for device removed errors",
                    false);

    RX_CONSOLE_BVAR(cvar_force_warp_adapter, "r.UseWapDriver", "Force using Microsoft's reference DirectX driver", false);

    void print_debug_message(
        D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context);

    RenderBackend::RenderBackend(HWND window_handle, const glm::uvec2& window_size)
        : command_lists_to_submit_on_end_frame{static_cast<Size>(cvar_max_in_flight_gpu_frames->get())},
          copy_command_lists_to_submit_on_end_frame{static_cast<Size>(cvar_max_in_flight_gpu_frames->get())},
          buffer_deletion_list{static_cast<Size>(cvar_max_in_flight_gpu_frames->get())},
          texture_deletion_list{static_cast<Size>(cvar_max_in_flight_gpu_frames->get())},
          staging_buffers_to_free{static_cast<Size>(cvar_max_in_flight_gpu_frames->get())},
          scratch_buffers_to_free{static_cast<Size>(cvar_max_in_flight_gpu_frames->get())} {
#ifndef NDEBUG
        if(*cvar_enable_debug_layers) {
            // Only enable the debug layer if we're not running in PIX
            const auto result = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&graphics_analysis));
            if(FAILED(result)) {
                enable_debugging();
            }
        }
#endif

        initialize_dxgi();

        select_adapter();

        create_queues();

        create_swapchain(window_handle, window_size);

        create_gpu_frame_synchronization_objects();

        create_descriptor_heaps();

        initialize_swapchain_descriptors();

        initialize_dma();

        create_standard_root_signature();

        create_pipeline_input_layouts();

        create_command_signatures();

        const auto num_frames = static_cast<Size>(cvar_max_in_flight_gpu_frames->get());
        in_use_direct_command_allocators.resize(num_frames);
        in_use_copy_command_allocators.resize(num_frames);

        logger->info("Initialized D3D12 render device");
    }

    RenderBackend::~RenderBackend() {
        wait_idle();

        staging_buffers.each_fwd([&](const Buffer& buffer) { buffer.allocation->Release(); });

        TracyD3D12Destroy(tracy_render_context);

        device_allocator->Release();
    }

    Rx::Optional<Buffer> RenderBackend::create_buffer(const BufferCreateInfo& create_info,
                                                      const D3D12_RESOURCE_FLAGS additional_flags) const {
        ZoneScoped;
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(create_info.size);
        desc.Flags = additional_flags;

        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
        bool should_map = false;

        D3D12MA::ALLOCATION_DESC alloc_desc{};
        switch(create_info.usage) {
            case BufferUsage::StagingBuffer:
                [[fallthrough]];
            case BufferUsage::ConstantBuffer:
                alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
                initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
                should_map = true;
                break;

            case BufferUsage::UnorderedAccess:
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                [[fallthrough]];
            case BufferUsage::IndirectCommands: // NOLINT(bugprone-branch-clone)
                [[fallthrough]];
            case BufferUsage::IndexBuffer:
                [[fallthrough]];
            case BufferUsage::VertexBuffer:
                alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
                initial_state = D3D12_RESOURCE_STATE_COMMON;
                break;

            case BufferUsage::RaytracingAccelerationStructure:
                alloc_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
                initial_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                break;

            default:
                logger->warning("Unknown buffer usage %u", create_info.usage);
        }

        auto buffer = Buffer{};
        const auto result = device_allocator->CreateResource(&alloc_desc,
                                                             &desc,
                                                             initial_state,
                                                             nullptr,
                                                             &buffer.allocation,
                                                             IID_PPV_ARGS(&buffer.resource));
        if(FAILED(result)) {
            logger->error("Could not create buffer %s: %s", create_info.name, to_string(result));
            return Rx::nullopt;
        }

        if(should_map) {
            D3D12_RANGE mapped_range{0, create_info.size};
            buffer.resource->Map(0, &mapped_range, &buffer.mapped_ptr);
        }

        buffer.size = create_info.size;

        buffer.name = create_info.name;

        set_object_name(buffer.resource, create_info.name);

        return buffer;
    }

    Rx::Optional<Texture> RenderBackend::create_texture(const TextureCreateInfo& create_info) const {
        auto format = to_dxgi_format(create_info.format); // TODO: Different to_dxgi_format functions for the different kinds of things
        if(format == DXGI_FORMAT_D32_FLOAT) {
            format = DXGI_FORMAT_R32_TYPELESS; // Create depth buffers with a TYPELESS format
        }
        D3D12_RESOURCE_DESC desc;
        if(create_info.depth == 1 || create_info.depth == 0) {
            desc = CD3DX12_RESOURCE_DESC::Tex2D(format,
                                                static_cast<Uint32>(round(create_info.width)),
                                                static_cast<Uint32>(round(create_info.height)));
        } else {
            desc = CD3DX12_RESOURCE_DESC::Tex3D(format, create_info.width, create_info.height, create_info.depth);
        }

        D3D12MA::ALLOCATION_DESC alloc_desc{.HeapType = D3D12_HEAP_TYPE_DEFAULT};

        if(create_info.enable_resource_sharing) {
            alloc_desc.ExtraHeapFlags |= D3D12_HEAP_FLAG_SHARED;
        }

        Texture texture;
        texture.format = create_info.format;

        switch(create_info.usage) {
            case TextureUsage::RenderTarget:
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                alloc_desc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED; // Render targets are always committed resources
                break;

            case TextureUsage::SampledTexture:
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                break;

            case TextureUsage::DepthStencil:
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                alloc_desc.Flags |= D3D12MA::ALLOCATION_FLAG_COMMITTED; // Depth/Stencil targets are always committed resources
                break;

            case TextureUsage::UnorderedAccess:
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                break;
        }

        const auto result = device_allocator->CreateResource(&alloc_desc,
                                                             &desc,
                                                             D3D12_RESOURCE_STATE_COMMON,
                                                             nullptr,
                                                             &texture.allocation,
                                                             IID_PPV_ARGS(&texture.resource));
        if(FAILED(result)) {
            logger->error("Could not create texture %s", create_info.name);
            return Rx::nullopt;
        }

        // logger->verbose("Created texture %s (%dx%d) with initial state %s",
        //                 create_info.name,
        //                 create_info.width,
        //                 create_info.height,
        //                 resource_state_to_string(initial_state));

        texture.name = create_info.name;
        texture.width = static_cast<Uint32>(desc.Width);
        texture.height = desc.Height;
        texture.depth = desc.DepthOrArraySize;

        set_object_name(texture.resource, create_info.name);

        return texture;
    }

    DescriptorRange RenderBackend::create_rtv_handle(const Texture& texture) const {
        const auto handle = rtv_allocator->allocate_descriptors(1);

        device->CreateRenderTargetView(texture.resource, nullptr, handle.cpu_handle);

        return handle;
    }

    DescriptorRange RenderBackend::create_dsv_handle(const Texture& texture) const {
        const auto desc = D3D12_DEPTH_STENCIL_VIEW_DESC{
            .Format = to_dxgi_format(texture.format),
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Texture2D = {.MipSlice = 0},
        };

        const auto handle = dsv_allocator->allocate_descriptors(1);

        device->CreateDepthStencilView(texture.resource, &desc, handle.cpu_handle);

        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE RenderBackend::get_backbuffer_rtv_handle() {
        const auto cur_swapchain_index = swapchain->GetCurrentBackBufferIndex();

        RX_ASSERT(cur_swapchain_index < swapchain_rtv_handles.size(),
                  "Not enough swapchain RTVs for current swapchain index %d",
                  cur_swapchain_index);

        return swapchain_rtv_handles[cur_swapchain_index].cpu_handle;
    }

    Uint2 RenderBackend::get_backbuffer_size() const {
        Uint2 vec;
        swapchain->GetSourceSize(&vec.x, &vec.y);

        return vec;
    }

    void* RenderBackend::map_buffer(const Buffer& buffer) const {
        void* ptr;
        D3D12_RANGE range{0, buffer.size};
        const auto result = buffer.resource->Map(0, &range, &ptr);
        if(FAILED(result)) {
            logger->error("Could not map buffer");
            return nullptr;
        }

        return ptr;
    }

    void RenderBackend::schedule_buffer_destruction(const Buffer& buffer) { buffer_deletion_list[cur_gpu_frame_idx].push_back(buffer); }

    void RenderBackend::schedule_texture_destruction(const Texture& texture) {
        texture_deletion_list[cur_gpu_frame_idx].push_back(texture);
    }

    ComPtr<ID3D12PipelineState> RenderBackend::create_compute_pipeline_state(const Rx::Vector<Uint8>& compute_shader) const {
        return create_compute_pipeline_state(compute_shader, standard_root_signature);
    }

    ComPtr<ID3D12PipelineState> RenderBackend::create_compute_pipeline_state(const Rx::Vector<Uint8>& compute_shader,
                                                                             const ComPtr<ID3D12RootSignature>& root_signature) const {
        const auto desc = D3D12_COMPUTE_PIPELINE_STATE_DESC{
            .pRootSignature = root_signature,
            .CS =
                {
                    .pShaderBytecode = compute_shader.data(),
                    .BytecodeLength = compute_shader.size(),
                },
        };

        ComPtr<ID3D12PipelineState> pso;
        const auto result = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
        if(FAILED(result)) {
            logger->error("Could not create compute pipeline: %s", to_string(result));
            return {};
        }

        store_com_interface(pso, *root_signature);

        return pso;
    }

    Rx::Ptr<RenderPipelineState> RenderBackend::create_render_pipeline_state(const RenderPipelineStateCreateInfo& create_info) {
        return create_pipeline_state(create_info, standard_root_signature);
    }

    ComPtr<ID3D12CommandAllocator> RenderBackend::get_or_create_command_allocator(const D3D12_COMMAND_LIST_TYPE type) {
        if(type == D3D12_COMMAND_LIST_TYPE_DIRECT && !direct_command_allocators.is_empty()) {
            const auto allocator = direct_command_allocators.last();
            direct_command_allocators.pop_back();
            return allocator;
        }

        if(type == D3D12_COMMAND_LIST_TYPE_COPY && !copy_command_allocators.is_empty()) {
            const auto allocator = copy_command_allocators.last();
            copy_command_allocators.pop_back();
            return allocator;
        }

        ComPtr<ID3D12CommandAllocator> allocator;
        // Hardcode to direct command list for now
        // TODO: Upgrade to a real copy command list at some point
        const auto result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
        if(FAILED(result)) {
            logger->error("Could not create command allocator of type %d: %s", D3D12_COMMAND_LIST_TYPE_DIRECT, result);
            return {};
        }

        if(type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            set_object_name(allocator, "Direct allocator");
        } else if(type == D3D12_COMMAND_LIST_TYPE_COPY) {
            set_object_name(allocator, "Copy allocator");
        }

        return allocator;
    }

    ComPtr<ID3D12GraphicsCommandList4> RenderBackend::create_render_command_list(Rx::Optional<Uint32> frame_idx) {
        if(!frame_idx) {
            frame_idx = cur_gpu_frame_idx;
        }

        // auto command_allocator = get_direct_command_allocator_for_thread(frame_idx, thread_idx);

        const auto command_allocator = get_or_create_command_allocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
        command_allocator->Reset();

        ComPtr<ID3D12CommandList> cmds;
        const auto result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&cmds));
        log_dred_report();
        if(FAILED(result)) {
            Rx::abort("Could not create command list: %s", to_string(result));
        }

        ID3D12GraphicsCommandList4* commands = nullptr;
        cmds->QueryInterface(&commands);
        if(!commands) {
            Rx::abort("Could not cast to ID3D12GraphicsCommandList4: %s", to_string(result));
        }

        commands->SetName(L"Unnamed Sanity Engine command list");
        store_com_interface(commands, *command_allocator);
        const auto gpu_frame_idx = GpuFrameIdx{*frame_idx};
        commands->SetPrivateData(PRIVATE_DATA_ATTRIBS(GpuFrameIdx), &gpu_frame_idx);
        command_lists_outside_render_device.fetch_add(1);

        return commands;
    }

    CopyCommandList RenderBackend::create_copy_command_list() {
        const auto command_allocator = get_or_create_command_allocator(D3D12_COMMAND_LIST_TYPE_COPY);
        command_allocator->Reset();

        ComPtr<ID3D12CommandList> cmds;
        const auto result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator, nullptr, IID_PPV_ARGS(&cmds));
        if(FAILED(result)) {
            Rx::abort("Could not create command list: %s", to_string(result));
        }

        ID3D12GraphicsCommandList4* commands = nullptr;
        cmds->QueryInterface(&commands);
        if(!commands) {
            Rx::abort("Could not cast to ID3D12GraphicsCommandList4: %s", to_string(result));
        }

        commands->SetName(L"Unnamed Sanity Engine command list");
        store_com_interface(commands, *command_allocator);
        command_lists_outside_render_device.fetch_add(1);

        return {*this, commands};
    }

    void RenderBackend::submit_command_list(ComPtr<ID3D12GraphicsCommandList4> commands) {
        const auto result = commands->Close();
        if(FAILED(result)) {
#ifndef NDEBUG
            Rx::abort("Could not close command list: %s", to_string(result));
#else
            logger->error("Could not close command list: %s", to_string(result));
#endif
        }

        const auto frame_idx = retrieve_object<GpuFrameIdx>(commands).idx;
        command_lists_to_submit_on_end_frame[frame_idx].push_back(commands);

        const auto allocator = get_com_interface<ID3D12CommandAllocator>(commands);
        in_use_direct_command_allocators[frame_idx].push_back(allocator);
    }

    void RenderBackend::submit_copy_command_list(const ComPtr<ID3D12GraphicsCommandList4> cmds) {
        copy_command_lists_to_submit_on_end_frame[cur_gpu_frame_idx].push_back(cmds);

        const auto allocator = get_com_interface<ID3D12CommandAllocator>(cmds);
        in_use_copy_command_allocators[cur_gpu_frame_idx].push_back(allocator);
    }

    void RenderBackend::begin_frame(const uint64_t frame_count) {
        ZoneScoped;

        flush_copy_command_lists();

        // Synchronize copy queue
        async_copy_queue->Signal(copy_queue_sync_fence, frame_count);
        direct_command_queue->Wait(copy_queue_sync_fence, frame_count);

        direct_command_queue->Signal(direct_command_ready_fence, frame_fence_values[cur_gpu_frame_idx]);

        // We wait on the direct queue, the direct queue waits on the copy queue, thus we implicitly wait on the copy queue
        wait_for_frame(cur_gpu_frame_idx);
        frame_fence_values[cur_gpu_frame_idx] = frame_count;

        cur_swapchain_idx = swapchain->GetCurrentBackBufferIndex();

        // Don't reset per frame resources on the first frame. This allows the engine to submit work while initializing
        if(!in_init_phase) {
            return_staging_buffers_for_frame(cur_gpu_frame_idx);

            copy_command_allocators.append(in_use_copy_command_allocators[cur_gpu_frame_idx]);
            direct_command_allocators.append(in_use_direct_command_allocators[cur_gpu_frame_idx]);

            in_use_copy_command_allocators[cur_gpu_frame_idx].clear();
            in_use_direct_command_allocators[cur_gpu_frame_idx].clear();

            destroy_resources_for_frame(cur_gpu_frame_idx);
        }

        transition_swapchain_texture_to_render_target();

        in_init_phase = false;
    }

    void RenderBackend::end_frame() {
        ZoneScoped;

        // Flush our logs before the debug layer issues any breakpoints
        Rx::Log::flush();

        transition_swapchain_texture_to_presentable();

        flush_batched_command_lists();

        {
            ZoneScopedN("Present");
            auto result = swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
            if(result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
                log_dred_report();

                result = device->GetDeviceRemovedReason();

                Rx::abort("Device lost on present: %s", to_string(result));
            }
        }

        if(is_frame_capture_active) {
            end_capture();
            is_frame_capture_active = false;
        }

#ifdef TRACY_ENABLE
        TracyD3D12NewFrame(renderer::RenderBackend::tracy_render_context);
        TracyD3D12Collect(renderer::RenderBackend::tracy_render_context);

        TracyD3D12NewFrame(renderer::RenderBackend::tracy_copy_context);
        TracyD3D12Collect(renderer::RenderBackend::tracy_copy_context);
#endif

        cur_gpu_frame_idx = (cur_gpu_frame_idx + 1) % cvar_max_in_flight_gpu_frames->get();
    }

    Uint32 RenderBackend::get_cur_gpu_frame_idx() const { return cur_gpu_frame_idx; }

    void RenderBackend::begin_capture() const {
        if(graphics_analysis) {
            graphics_analysis->BeginCapture();
        }
    }

    void RenderBackend::begin_frame_capture() {
        if(!is_frame_capture_active) {
            begin_capture();
        }

        is_frame_capture_active = true;
    }

    void RenderBackend::end_capture() const {
        if(graphics_analysis) {
            graphics_analysis->EndCapture();
        }
    }

    void RenderBackend::wait_idle() {
        const auto num_gpu_frames = static_cast<Uint32>(cvar_max_in_flight_gpu_frames->get());
        for(Uint32 i = 0; i < num_gpu_frames; i++) {
            wait_for_frame(i);
            direct_command_queue->Wait(direct_command_ready_fence, frame_fence_values[i]);
        }

        wait_gpu_idle(0);
    }

    Uint32 RenderBackend::get_max_num_gpu_frames() const { return static_cast<Uint32>(cvar_max_in_flight_gpu_frames->get()); }

    bool RenderBackend::has_separate_device_memory() const { return !is_uma; }

    Buffer RenderBackend::get_staging_buffer(const Uint64 num_bytes, const Uint64 alignment) {
        ZoneScoped;

        for(size_t i = 0; i < staging_buffers.size(); i++) {
            if(staging_buffers[i].size >= num_bytes && staging_buffers[i].alignment == alignment) {
                // Return the first suitable buffer we find
                auto buffer = Rx::Utility::move(staging_buffers[i]);
                staging_buffers.erase(i, i + 1);

                return buffer;
            }
        }

        // No suitable buffer is available, let's make a new one
        return create_staging_buffer(num_bytes, alignment);
    }

    Buffer RenderBackend::get_staging_buffer_for_texture(ID3D12Resource* texture) {
        auto desc = texture->GetDesc();
        Uint64 required_size{0};
        device->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &required_size);

        return get_staging_buffer(required_size);
    }

    void RenderBackend::return_staging_buffer(const Buffer& buffer) {
        staging_buffers_to_free[cur_gpu_frame_idx].push_back(Rx::Utility::move(buffer));
    }

    Buffer RenderBackend::get_scratch_buffer(const Uint32 num_bytes) {
        size_t best_fit_idx = scratch_buffers.size();
        for(size_t i = 0; i < scratch_buffers.size(); i++) {
            if(scratch_buffers[i].size >= num_bytes) {
                if(best_fit_idx >= scratch_buffers.size() || scratch_buffers[i].size < scratch_buffers[best_fit_idx].size) {
                    // The current buffer is a better fit than the previous best fit buffer
                    best_fit_idx = i;
                }
            }
        }

        if(best_fit_idx < scratch_buffers.size()) {
            // We already have a suitable scratch buffer!
            auto buffer = Rx::Utility::move(scratch_buffers[best_fit_idx]);
            scratch_buffers.erase(best_fit_idx, best_fit_idx);

            return buffer;

        } else {
            return create_scratch_buffer(num_bytes);
        }
    }

    void RenderBackend::return_scratch_buffer(const Buffer& buffer) {
        scratch_buffers_to_free[cur_gpu_frame_idx].push_back(Rx::Utility::move(buffer));
    }

    ID3D12Device* RenderBackend::get_d3d12_device() const { return device; }

    void RenderBackend::enable_debugging() {
        auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller));
        if(SUCCEEDED(result)) {
            debug_controller->EnableDebugLayer();

            if(cvar_enable_gpu_based_validation->get()) {
                debug_controller->SetEnableGPUBasedValidation(1);
            }

        } else {
            logger->error("Could not enable the D3D12 validation layer: %s", to_string(result).data());
        }

        result = D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings));
        if(FAILED(result)) {
            logger->error("Could not enable DRED");

        } else {
            dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred_settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }

    void RenderBackend::initialize_dxgi() {
        ZoneScoped;

        auto flags = 0;
        if(*cvar_enable_debug_layers) {
            flags = DXGI_CREATE_FACTORY_DEBUG;
        }

        ComPtr<IDXGIFactory2> basic_factory;
        const auto result = CreateDXGIFactory2(flags, IID_PPV_ARGS(&basic_factory));
        if(FAILED(result)) {
            Rx::abort("Could not initialize DXGI");
        }

        factory = basic_factory.as<IDXGIFactory4>();
        if(!factory) {
            Rx::abort("DXGI is not at a new enough version, please update your graphics drivers");
        }
    }

    void RenderBackend::select_adapter() {
        ZoneScoped;

        // We want an adapter:
        // - Not integrated, if possible

        // TODO: Figure out how to get the number of adapters in advance
        Rx::Vector<ComPtr<IDXGIAdapter>> adapters;
        adapters.reserve(5);

        {
            UINT adapter_idx = 0;
            IDXGIAdapter* cur_adapter;
            while(factory->EnumAdapters(adapter_idx, &cur_adapter) != DXGI_ERROR_NOT_FOUND) {
                auto ptr = ComPtr<IDXGIAdapter>{cur_adapter};
                adapters.push_back(ptr);
                adapter_idx++;
            }
        }

        {
            if(cvar_force_warp_adapter->get()) {
                ComPtr<IDXGIAdapter> cur_adapter;
                const auto result = factory->EnumWarpAdapter(IID_PPV_ARGS(&cur_adapter));

                if(FAILED(result)) {
                    logger->warning("Could not get the WARP adapter: %s", to_string(result));

                } else {
                    adapters.clear();
                    adapters.push_back(cur_adapter);
                }
            }
        }

        // TODO: Score adapters based on things like supported feature level and available vram

        adapters.each_fwd([&](const ComPtr<IDXGIAdapter>& cur_adapter) {
            DXGI_ADAPTER_DESC desc;
            cur_adapter->GetDesc(&desc);

            if(desc.VendorId == INTEL_PCI_VENDOR_ID && adapters.size() > 1) {
                // If there's a GPU other than an Intel GPU available,
                return RX_ITERATION_CONTINUE;
            }

            ComPtr<ID3D12Device> try_device;
            auto res = D3D12CreateDevice(cur_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&try_device));
            if(SUCCEEDED(res)) {
                // check the features we care about
                D3D12_FEATURE_DATA_D3D12_OPTIONS d3d12_options;
                try_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &d3d12_options, sizeof(d3d12_options));
                if(d3d12_options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_3) {
                    // Resource binding tier three means we can have partially bound descriptor array. Nova relies on partially bound
                    // descriptor arrays, so we need it
                    // Thus - if we find an adapter without full descriptor indexing support, we ignore it

                    logger->warning("Ignoring adapter %s - Doesn't have the flexible resource binding that Sanity Engine needs",
                                    Rx::WideString{reinterpret_cast<const Uint16*>(desc.Description)}.to_utf8());

                    return RX_ITERATION_CONTINUE;
                }

                D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
                res = try_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
                if(FAILED(res)) {
                    logger->warning("Ignoring adapter %s - Could not check the supported shader model: %s",
                                    Rx::WideString{reinterpret_cast<const Uint16*>(desc.Description)}.to_utf8(),
                                    to_string(res));

                    return RX_ITERATION_CONTINUE;

                } else if(shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_5) {
                    // Only supports old-ass shaders

                    logger->warning("Ignoring adapter %s - Doesn't support shader model 6.5",
                                    Rx::WideString{reinterpret_cast<const Uint16*>(desc.Description)}.to_utf8());
                    return RX_ITERATION_CONTINUE;
                }

                adapter = cur_adapter;

                device = try_device.as<ID3D12Device5>();

                // Save information about the device
                D3D12_FEATURE_DATA_ARCHITECTURE arch{};
                res = device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &arch, sizeof(D3D12_FEATURE_DATA_ARCHITECTURE));
                if(SUCCEEDED(res)) {
                    is_uma = arch.CacheCoherentUMA;
                }

                D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
                res = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
                if(SUCCEEDED(res)) {
                    render_pass_tier = options5.RenderPassesTier;
                    has_raytracing = options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
                }

#ifndef NDEBUG
                info_queue = device.as<ID3D12InfoQueue>();
                if(info_queue && cvar_break_on_validation_error->get()) {
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
                    info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
                }

                // TODO: Un-comment when I get Windows build 20236 or later
                // ComPtr<ID3D12InfoQueue1> cool_queue;
                // device.As(&cool_queue);
                // if(cool_queue) {
                //     cool_queue->RegisterMessageCallback(print_debug_message, D3D12_MESSAGE_CALLBACK_FLAG_NONE, d3d12_logger.data(),
                //     &debug_message_callback_cookie);
                // }
#endif

                return RX_ITERATION_STOP;

            } else {
                logger->warning("Ignoring adapter %s - doesn't support D3D12",
                                Rx::WideString{reinterpret_cast<const Uint16*>(desc.Description)}.to_utf8());
            }

            return RX_ITERATION_CONTINUE;
        });

        if(!device) {
            Rx::abort("Could not find a suitable D3D12 adapter");
        }

        set_object_name(device, "D3D12 Device");
    }

    void RenderBackend::create_queues() {
        ZoneScoped;

        // One graphics queue and one optional DMA queue
        D3D12_COMMAND_QUEUE_DESC graphics_queue_desc{};
        graphics_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        graphics_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        graphics_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        auto result = device->CreateCommandQueue(&graphics_queue_desc, IID_PPV_ARGS(&direct_command_queue));
        if(FAILED(result)) {
            Rx::abort("Could not create graphics command queue");
        }

        set_object_name(direct_command_queue, "Render Queue");

        if(!is_uma) {
            // No need to care about DMA on UMA cause we can just map everything
            D3D12_COMMAND_QUEUE_DESC dma_queue_desc{};
            dma_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
            dma_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            dma_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

            result = device->CreateCommandQueue(&dma_queue_desc, IID_PPV_ARGS(&async_copy_queue));
            if(FAILED(result)) {
                logger->warning("Could not create a DMA queue on a non-UMA adapter, data transfers will have to use the graphics queue");

            } else {
                set_object_name(async_copy_queue, "DMA queue");
            }
        }

#ifdef TRACY_ENABLE
        tracy_render_context = TracyD3D12Context(device, direct_command_queue);
        tracy_copy_context = TracyD3D12Context(device, async_copy_queue);
#endif
    }

    void RenderBackend::create_swapchain(HWND window_handle, const glm::uvec2& window_size) {
        ZoneScoped;

        logger->verbose("Creating swapchain with resolution %dx%d", window_size.x, window_size.y);

        const auto swapchain_desc = DXGI_SWAP_CHAIN_DESC1{
            .Width = static_cast<UINT>(window_size.x),
            .Height = static_cast<UINT>(window_size.y),
            .Format = swapchain_format,
            .SampleDesc = {1, 0},

            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = static_cast<Uint32>(cvar_max_in_flight_gpu_frames->get()),

            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
            .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,
        };

        ComPtr<IDXGISwapChain1> swapchain1;
        auto hr = factory->CreateSwapChainForHwnd(direct_command_queue, window_handle, &swapchain_desc, nullptr, nullptr, &swapchain1);
        if(FAILED(hr)) {
            const auto readable_result = to_string(hr);
            Rx::abort("Could not create swapchain: %s", readable_result);
        }

        swapchain = swapchain1.as<IDXGISwapChain3>();
        if(FAILED(hr)) {
            Rx::abort("Could not get new swapchain interface, please update your drivers");
        }
    }

    void RenderBackend::create_gpu_frame_synchronization_objects() {
        frame_fence_values.resize(cvar_max_in_flight_gpu_frames->get());

        device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&direct_command_ready_fence));
        set_object_name(direct_command_ready_fence, "Direct Queue Fence");

        frame_event = CreateEvent(nullptr, false, false, nullptr);

        device->CreateFence(1, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_queue_sync_fence));
        set_object_name(copy_queue_sync_fence, "Copy Queue Fence");
    }

    void RenderBackend::create_descriptor_heaps() {
        ZoneScoped;

        const auto total_num_buffers = *cvar_max_in_flight_gpu_frames * MAX_NUM_BUFFERS;
        const auto total_num_textures = *cvar_max_in_flight_gpu_frames * MAX_NUM_TEXTURES * 2;
        const auto
            num_bespoke_descriptors = 65536; // Descriptors for the RT AS or single-pass downsampler or whatever else wants descriptors

        const auto [new_cbv_srv_uav_heap,
                    new_cbv_srv_uav_size] = create_descriptor_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                   total_num_buffers + total_num_textures + num_bespoke_descriptors);
        set_object_name(new_cbv_srv_uav_heap, "CBV/SRV/UAV Heap");

        cbv_srv_uav_allocator = Rx::make_ptr<DescriptorAllocator>(RX_SYSTEM_ALLOCATOR, new_cbv_srv_uav_heap, new_cbv_srv_uav_size);

        const auto [rtv_heap, rtv_size] = create_descriptor_heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024);
        set_object_name(rtv_heap, "RTV Heap");
        rtv_allocator = Rx::make_ptr<DescriptorAllocator>(RX_SYSTEM_ALLOCATOR, rtv_heap, rtv_size);

        const auto [dsv_heap, dsv_size] = create_descriptor_heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 32);
        set_object_name(dsv_heap, "DSV Heap");
        dsv_allocator = Rx::make_ptr<DescriptorAllocator>(RX_SYSTEM_ALLOCATOR, dsv_heap, dsv_size);
    }

    void RenderBackend::initialize_swapchain_descriptors() {
        DXGI_SWAP_CHAIN_DESC1 desc;
        swapchain->GetDesc1(&desc);
        swapchain_textures.resize(desc.BufferCount);
        swapchain_rtv_handles.reserve(desc.BufferCount);

        for(Uint32 i = 0; i < desc.BufferCount; i++) {
            swapchain->GetBuffer(i, IID_PPV_ARGS(&swapchain_textures[i]));

            const auto rtv_handle = rtv_allocator->allocate_descriptors(1);

            device->CreateRenderTargetView(swapchain_textures[i], nullptr, rtv_handle.cpu_handle);

            swapchain_rtv_handles.push_back(rtv_handle);

            set_object_name(swapchain_textures[i], Rx::String::format("Swapchain texture %d", i));
        }
    }

    std::pair<ComPtr<ID3D12DescriptorHeap>, UINT> RenderBackend::create_descriptor_heap(const D3D12_DESCRIPTOR_HEAP_TYPE descriptor_type,
                                                                                        const Uint32 num_descriptors) const {
        ComPtr<ID3D12DescriptorHeap> heap;

        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.Type = descriptor_type;
        heap_desc.NumDescriptors = num_descriptors;
        heap_desc.Flags = (descriptor_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE :
                                                                                       D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

        const auto result = device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap));
        if(FAILED(result)) {
            logger->error("Could not create descriptor heap: %s", to_string(result));

            return {{}, 0};
        }

        const auto descriptor_size = device->GetDescriptorHandleIncrementSize(descriptor_type);

        return {heap, descriptor_size};
    }

    void RenderBackend::initialize_dma() {
        ZoneScoped;

        D3D12MA::ALLOCATOR_DESC allocator_desc{};
        allocator_desc.pDevice = device;
        allocator_desc.pAdapter = adapter;

        const auto result = D3D12MA::CreateAllocator(&allocator_desc, &device_allocator);
        if(FAILED(result)) {
            Rx::abort("Could not initialize DMA");
        }
    }

    void RenderBackend::create_standard_root_signature() {
        ZoneScoped;

        Rx::Vector<CD3DX12_ROOT_PARAMETER> root_parameters{3};

        // Root constants for indices and IDs
        root_parameters[ROOT_CONSTANTS_ROOT_PARAMETER_INDEX].InitAsConstants(sizeof(StandardPushConstants) / 4, 0);

        // Raytracing data
        root_parameters[RAYTRACING_SCENE_ROOT_PARAMETER_INDEX].InitAsShaderResourceView(0);

        // Resources
        const auto resource_table_descriptor_ranges = Rx::Array{
            // SRV buffers
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                .NumDescriptors = MAX_NUM_BUFFERS,
                .BaseShaderRegister = 16,
                .RegisterSpace = 0,
                .OffsetInDescriptorsFromTableStart = 0,
            },

            // UAV buffers
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                .NumDescriptors = MAX_NUM_BUFFERS,
                .BaseShaderRegister = 16,
                .RegisterSpace = 1,
                .OffsetInDescriptorsFromTableStart = 0,
            },

            // Texture2D
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                .NumDescriptors = MAX_NUM_TEXTURES,
                .BaseShaderRegister = 16,
                .RegisterSpace = 16,
                .OffsetInDescriptorsFromTableStart = MAX_NUM_BUFFERS + SRV_OFFSET,
            },

            // RWTexture2D<float4>
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                .NumDescriptors = MAX_NUM_TEXTURES,
                .BaseShaderRegister = 16,
                .RegisterSpace = 20,
                .OffsetInDescriptorsFromTableStart = MAX_NUM_BUFFERS + UAV_OFFSET,
            },

            // Texture3D
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                .NumDescriptors = MAX_NUM_TEXTURES,
                .BaseShaderRegister = 16,
                .RegisterSpace = 32,
                .OffsetInDescriptorsFromTableStart = MAX_NUM_BUFFERS + SRV_OFFSET,
            },

            // RWTexture3D<float4>
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                .NumDescriptors = MAX_NUM_TEXTURES,
                .BaseShaderRegister = 16,
                .RegisterSpace = 36,
                .OffsetInDescriptorsFromTableStart = MAX_NUM_BUFFERS + UAV_OFFSET,
            },

            // RWTexture3D<float2>
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                .NumDescriptors = MAX_NUM_TEXTURES,
                .BaseShaderRegister = 16,
                .RegisterSpace = 37,
                .OffsetInDescriptorsFromTableStart = MAX_NUM_BUFFERS + UAV_OFFSET,
            },

            // RWTexture3D<float4>
            D3D12_DESCRIPTOR_RANGE{
                .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                .NumDescriptors = MAX_NUM_TEXTURES,
                .BaseShaderRegister = 16,
                .RegisterSpace = 38,
                .OffsetInDescriptorsFromTableStart = MAX_NUM_BUFFERS + UAV_OFFSET,
            },
        };
        root_parameters[RESOURCES_ARRAY_ROOT_PARAMETER_INDEX].InitAsDescriptorTable(static_cast<UINT>(
                                                                                        resource_table_descriptor_ranges.size()),
                                                                                    resource_table_descriptor_ranges.data());

        Rx::Vector<D3D12_STATIC_SAMPLER_DESC> static_samplers{3};

        auto& point_sampler = static_samplers[0];
        point_sampler = this->point_sampler_desc;

        auto& linear_sampler = static_samplers[1];
        linear_sampler = linear_sampler_desc;
        linear_sampler.ShaderRegister = 1;

        auto& trilinear_sampler = static_samplers[2];
        trilinear_sampler = trilinear_sampler_desc;
        trilinear_sampler.MaxAnisotropy = 8;
        trilinear_sampler.ShaderRegister = 2;

        D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
        root_signature_desc.NumParameters = static_cast<UINT>(root_parameters.size());
        root_signature_desc.pParameters = root_parameters.data();
        root_signature_desc.NumStaticSamplers = static_cast<UINT>(static_samplers.size());
        root_signature_desc.pStaticSamplers = static_samplers.data();
        root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        standard_root_signature = compile_root_signature(root_signature_desc);
        if(!standard_root_signature) {
            Rx::abort("Could not create standard root signature");
        }

        set_object_name(standard_root_signature, "Standard Root Signature");
    }

    ComPtr<ID3D12RootSignature> RenderBackend::compile_root_signature(const D3D12_ROOT_SIGNATURE_DESC& root_signature_desc) const {
        ZoneScoped;

        const auto versioned_desc = D3D12_VERSIONED_ROOT_SIGNATURE_DESC{.Version = D3D_ROOT_SIGNATURE_VERSION_1_0,
                                                                        .Desc_1_0 = root_signature_desc};

        ComPtr<ID3DBlob> root_signature_blob;
        ComPtr<ID3DBlob> error_blob;
        auto result = D3D12SerializeVersionedRootSignature(&versioned_desc, &root_signature_blob, &error_blob);
        if(FAILED(result)) {
            const Rx::String msg{static_cast<char*>(error_blob->GetBufferPointer()), error_blob->GetBufferSize()};
            logger->error("Could not create root signature: %s", msg);
            return {};
        }

        ComPtr<ID3D12RootSignature> sig;
        result = device->CreateRootSignature(0,
                                             root_signature_blob->GetBufferPointer(),
                                             root_signature_blob->GetBufferSize(),
                                             IID_PPV_ARGS(&sig));
        if(FAILED(result)) {
            logger->error("Could not create root signature: %s", to_string(result));
            return {};
        }

        return sig;
    }

    ComPtr<ID3D12RootSignature> RenderBackend::get_standard_root_signature() const { return standard_root_signature; }

    DescriptorAllocator& RenderBackend::get_cbv_srv_uav_allocator() const { return *cbv_srv_uav_allocator; }

    ID3D12DescriptorHeap* RenderBackend::get_cbv_srv_uav_heap() const {
        ZoneScoped;
        return cbv_srv_uav_allocator->get_heap();
    }

    void RenderBackend::create_pipeline_input_layouts() {
        standard_graphics_pipeline_input_layout.reserve(4);

        standard_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Position",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R32G32B32_FLOAT,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});

        standard_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Normal",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R32G32B32_FLOAT,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});

        standard_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Color",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});

        standard_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Texcoord",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R32G32_FLOAT,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});

        dear_imgui_graphics_pipeline_input_layout.reserve(3);

        dear_imgui_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Position",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R32G32_FLOAT,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});

        dear_imgui_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Texcoord",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R32G32_FLOAT,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});

        dear_imgui_graphics_pipeline_input_layout.push_back(
            D3D12_INPUT_ELEMENT_DESC{.SemanticName = "Color",
                                     .SemanticIndex = 0,
                                     .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                                     .InputSlot = 0,
                                     .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                                     .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                                     .InstanceDataStepRate = 0});
    }

    void RenderBackend::create_command_signatures() {
        const auto argument_descs = Rx::Array{D3D12_INDIRECT_ARGUMENT_DESC{.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,
                                                                           .Constant =
                                                                               {.RootParameterIndex = ROOT_CONSTANTS_ROOT_PARAMETER_INDEX,
                                                                                .DestOffsetIn32BitValues = DATA_INDEX_ROOT_CONSTANT_OFFSET,
                                                                                .Num32BitValuesToSet = 1}},
                                              D3D12_INDIRECT_ARGUMENT_DESC{.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED}};
        const auto desc = D3D12_COMMAND_SIGNATURE_DESC{.ByteStride = sizeof(IndirectDrawCommandWithRootConstant),
                                                       .NumArgumentDescs = static_cast<UINT>(argument_descs.size()),
                                                       .pArgumentDescs = argument_descs.data()};
        device->CreateCommandSignature(&desc, standard_root_signature, IID_PPV_ARGS(&standard_drawcall_command_signature));
    }

    Rx::Vector<D3D12_SHADER_INPUT_BIND_DESC> RenderBackend::get_bindings_from_shader(const Rx::Vector<Uint8>& shader) const {
        ComPtr<ID3D12ShaderReflection> reflection;
        auto result = D3DReflect(shader.data(), shader.size() * sizeof(Uint8), IID_PPV_ARGS(&reflection));
        if(FAILED(result)) {
            logger->error("Could not retrieve shader reflection information: %s", to_string(result));
        }

        D3D12_SHADER_DESC desc;
        result = reflection->GetDesc(&desc);
        if(FAILED(result)) {
            logger->error("Could not get shader description");
        }

        Rx::Vector<D3D12_SHADER_INPUT_BIND_DESC> input_descs(desc.BoundResources);

        for(Uint32 i = 0; i < desc.BoundResources; i++) {
            result = reflection->GetResourceBindingDesc(i, &input_descs[i]);
            if(FAILED(result)) {
                logger->error("Could not get binding information for resource idx %u", i);
            }
        }

        return input_descs;
    }

    Rx::Ptr<RenderPipelineState> RenderBackend::create_pipeline_state(const RenderPipelineStateCreateInfo& create_info,
                                                                      ID3D12RootSignature* root_signature) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};

        desc.pRootSignature = root_signature;

        desc.VS.BytecodeLength = create_info.vertex_shader.size();
        desc.VS.pShaderBytecode = create_info.vertex_shader.data();

        if(create_info.pixel_shader) {
            desc.PS.BytecodeLength = create_info.pixel_shader->size();
            desc.PS.pShaderBytecode = create_info.pixel_shader->data();
        }

        switch(create_info.input_assembler_layout) {
            case InputAssemblerLayout::StandardVertex:
                desc.InputLayout.NumElements = static_cast<UINT>(standard_graphics_pipeline_input_layout.size());
                desc.InputLayout.pInputElementDescs = standard_graphics_pipeline_input_layout.data();
                break;

            case InputAssemblerLayout::DearImGui:
                desc.InputLayout.NumElements = static_cast<UINT>(dear_imgui_graphics_pipeline_input_layout.size());
                desc.InputLayout.pInputElementDescs = dear_imgui_graphics_pipeline_input_layout.data();
                break;
        }
        desc.PrimitiveTopologyType = to_d3d12_primitive_topology_type(create_info.primitive_type);

        // Rasterizer state
        {
            auto& output_rasterizer_state = desc.RasterizerState;
            const auto& rasterizer_state = create_info.rasterizer_state;

            output_rasterizer_state.FillMode = to_d3d12_fill_mode(rasterizer_state.fill_mode);
            output_rasterizer_state.CullMode = to_d3d12_cull_mode(rasterizer_state.cull_mode);
            output_rasterizer_state.FrontCounterClockwise = rasterizer_state.front_face_counter_clockwise ? 1 : 0;
            output_rasterizer_state.DepthBias = static_cast<UINT>(
                rasterizer_state.depth_bias); // TODO: Figure out what the actual fuck D3D12 depth bias is
            output_rasterizer_state.DepthBiasClamp = rasterizer_state.max_depth_bias;
            output_rasterizer_state.SlopeScaledDepthBias = rasterizer_state.slope_scaled_depth_bias;
            output_rasterizer_state.MultisampleEnable = rasterizer_state.num_msaa_samples > 1 ? 1 : 0;
            output_rasterizer_state.AntialiasedLineEnable = rasterizer_state.enable_line_antialiasing;
            output_rasterizer_state.ConservativeRaster = rasterizer_state.enable_conservative_rasterization ?
                                                             D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON :
                                                             D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            desc.SampleMask = UINT_MAX;
            desc.SampleDesc.Count = rasterizer_state.num_msaa_samples;
        }

        // Depth stencil state
        {
            auto& output_ds_state = desc.DepthStencilState;
            const auto& ds_state = create_info.depth_stencil_state;

            output_ds_state.DepthEnable = ds_state.enable_depth_test ? 1 : 0;
            output_ds_state.DepthWriteMask = ds_state.enable_depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            output_ds_state.DepthFunc = to_d3d12_comparison_func(ds_state.depth_func);

            output_ds_state.StencilEnable = ds_state.enable_stencil_test ? 1 : 0;
            output_ds_state.StencilReadMask = ds_state.stencil_read_mask;
            output_ds_state.StencilWriteMask = ds_state.stencil_write_mask;
            output_ds_state.FrontFace.StencilFailOp = to_d3d12_stencil_op(ds_state.front_face.fail_op);
            output_ds_state.FrontFace.StencilDepthFailOp = to_d3d12_stencil_op(ds_state.front_face.depth_fail_op);
            output_ds_state.FrontFace.StencilPassOp = to_d3d12_stencil_op(ds_state.front_face.pass_op);
            output_ds_state.FrontFace.StencilFunc = to_d3d12_comparison_func(ds_state.front_face.compare_op);
            output_ds_state.BackFace.StencilFailOp = to_d3d12_stencil_op(ds_state.back_face.fail_op);
            output_ds_state.BackFace.StencilDepthFailOp = to_d3d12_stencil_op(ds_state.back_face.depth_fail_op);
            output_ds_state.BackFace.StencilPassOp = to_d3d12_stencil_op(ds_state.back_face.pass_op);
            output_ds_state.BackFace.StencilFunc = to_d3d12_comparison_func(ds_state.back_face.compare_op);
        }

        // Blend state
        {
            const auto& blend_state = create_info.blend_state;
            desc.BlendState.AlphaToCoverageEnable = blend_state.enable_alpha_to_coverage ? 1 : 0;
            for(Uint32 i = 0; i < blend_state.render_target_blends.size(); i++) {
                auto& output_rt_blend = desc.BlendState.RenderTarget[i];
                const auto& rt_blend = blend_state.render_target_blends[i];

                output_rt_blend.BlendEnable = rt_blend.enabled ? 1 : 0;
                output_rt_blend.SrcBlend = to_d3d12_blend(rt_blend.source_color_blend_factor);
                output_rt_blend.DestBlend = to_d3d12_blend(rt_blend.destination_color_blend_factor);
                output_rt_blend.BlendOp = to_d3d12_blend_op(rt_blend.color_blend_op);
                output_rt_blend.SrcBlendAlpha = to_d3d12_blend(rt_blend.source_alpha_blend_factor);
                output_rt_blend.DestBlendAlpha = to_d3d12_blend(rt_blend.destination_alpha_blend_factor);
                output_rt_blend.BlendOpAlpha = to_d3d12_blend_op(rt_blend.alpha_blend_op);
                output_rt_blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
        }

        RX_ASSERT(create_info.render_target_formats.size() + (create_info.depth_stencil_format ? 1 : 0) > 0,
                  "Must have at least one render target or depth target");
        RX_ASSERT(create_info.render_target_formats.size() < 8,
                  "May not have more than 8 render targets - you have &d",
                  create_info.render_target_formats.size());

        desc.NumRenderTargets = static_cast<UINT>(create_info.render_target_formats.size());
        for(Uint32 i = 0; i < create_info.render_target_formats.size(); i++) {
            desc.RTVFormats[i] = to_dxgi_format(create_info.render_target_formats[i]);
        }
        if(create_info.depth_stencil_format) {
            desc.DSVFormat = to_dxgi_format(*create_info.depth_stencil_format);
        } else {
            desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        }

        auto pipeline = Rx::make_ptr<RenderPipelineState>(RX_SYSTEM_ALLOCATOR);
        pipeline->root_signature = ComPtr<ID3D12RootSignature>(root_signature);

        const auto result = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline->pso));
        if(FAILED(result)) {
            logger->error("Could not create render pipeline %s: %s", create_info.name, to_string(result));
            return {};
        }

        set_object_name(pipeline->pso, create_info.name);

        return pipeline;
    }

    void RenderBackend::flush_copy_command_lists() {
        auto& copy_lists = copy_command_lists_to_submit_on_end_frame[cur_gpu_frame_idx];
        copy_lists.each_fwd([&](const ComPtr<ID3D12GraphicsCommandList4>& cmds) {
            ID3D12CommandList* list = cmds;
            direct_command_queue->ExecuteCommandLists(1, &list);
        });
        command_lists_outside_render_device.fetch_sub(copy_lists.size());
        copy_lists.clear();
    }

    void RenderBackend::flush_batched_command_lists() {
        // TODO: Insert barriers/fences/other synchronization things so that the copy commands finish before these command lists begin
        // executing

        // Submit all the command lists we batched up
        auto& lists = command_lists_to_submit_on_end_frame[cur_gpu_frame_idx];
        lists.each_fwd([&](ComPtr<ID3D12GraphicsCommandList4>& commands) {
            auto* d3d12_command_list = static_cast<ID3D12CommandList*>(commands);

            // First implementation - run everything on the same queue, because it's easy
            // Eventually I'll come up with a fancy way to use multiple queues

            // TODO: Actually figure out how to use multiple queues
            direct_command_queue->ExecuteCommandLists(1, &d3d12_command_list);

            if(*cvar_verify_every_command_list_submission) {
                auto command_list_done_fence = get_next_command_list_done_fence();

                direct_command_queue->Signal(command_list_done_fence, CPU_FENCE_SIGNALED);

                // ReSharper disable once CppLocalVariableMayBeConst
                HANDLE event = CreateEvent(nullptr, false, false, nullptr);
                if(event != nullptr) {
                    command_list_done_fence->SetEventOnCompletion(CPU_FENCE_SIGNALED, event);

                    WaitForSingleObject(event, INFINITE);

                    log_dred_report();

                    command_list_done_fences.push_back(command_list_done_fence);

                    CloseHandle(event);
                } else {
                    logger->error("Could not create an event to use to wait on command lists");
                }
            }
        });

        command_lists_outside_render_device.fetch_sub(lists.size());

        command_lists_to_submit_on_end_frame[cur_gpu_frame_idx] = {};
    }

    void RenderBackend::return_staging_buffers_for_frame(const Uint32 frame_idx) {
        ZoneScoped;
        auto& staging_buffers_for_frame = staging_buffers_to_free[frame_idx];
        staging_buffers.append(staging_buffers_for_frame);
        staging_buffers_for_frame.clear();
    }

    void RenderBackend::destroy_resources_for_frame(const Uint32 frame_idx) {
        ZoneScoped;
        auto& buffers = buffer_deletion_list[frame_idx];
        buffers.clear();

        auto& textures = texture_deletion_list[cur_gpu_frame_idx];
        textures.clear();
    }

    void RenderBackend::transition_swapchain_texture_to_render_target() {
        ZoneScoped;
        auto swapchain_cmds = create_render_command_list(cur_gpu_frame_idx);
        set_object_name(swapchain_cmds, "RenderBackend::transition_swapchain_texture_to_render_target");

        {
            TracyD3D12Zone(tracy_render_context, *swapchain_cmds, "RenderBackend::transition_swapchain_texture_to_render_target");
            PIXScopedEvent(*swapchain_cmds, PIX_COLOR_DEFAULT, "RenderBackend::transition_swapchain_texture_to_render_target");

            D3D12_RESOURCE_BARRIER
            swapchain_transition_barrier = CD3DX12_RESOURCE_BARRIER::Transition(swapchain_textures[cur_swapchain_idx],
                                                                                D3D12_RESOURCE_STATE_PRESENT,
                                                                                D3D12_RESOURCE_STATE_RENDER_TARGET);
            swapchain_cmds->ResourceBarrier(1, &swapchain_transition_barrier);
        }

        submit_command_list(Rx::Utility::move(swapchain_cmds));
    }

   void RenderBackend::transition_swapchain_texture_to_presentable() {
        ZoneScoped;

        auto swapchain_cmds = create_render_command_list(cur_gpu_frame_idx);
        set_object_name(swapchain_cmds, "RenderBackend::transition_swapchain_texture_to_presentable");

        {
            TracyD3D12Zone(tracy_render_context, *swapchain_cmds, "RenderBackend::transition_swapchain_texture_to_presentable");
            PIXScopedEvent(*swapchain_cmds, PIX_COLOR_DEFAULT, "RenderBackend::transition_swapchain_texture_to_presentable");

            D3D12_RESOURCE_BARRIER
            swapchain_transition_barrier = CD3DX12_RESOURCE_BARRIER::Transition(swapchain_textures[cur_swapchain_idx],
                                                                                D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                                D3D12_RESOURCE_STATE_PRESENT);
            swapchain_cmds->ResourceBarrier(1, &swapchain_transition_barrier);
        }

        submit_command_list(Rx::Utility::move(swapchain_cmds));
    }

    void RenderBackend::wait_for_frame(const uint64_t frame_index) {
        ZoneScopedC(tracy::Color::MistyRose2);
        const auto desired_fence_value = frame_fence_values[frame_index];
        const auto initial_fence_value = direct_command_ready_fence->GetCompletedValue();

        if(initial_fence_value < desired_fence_value) {
            // If the fence's most recent value is not the value we want, then the GPU has not finished executing the frame and we need to
            // explicitly wait

            direct_command_ready_fence->SetEventOnCompletion(desired_fence_value, frame_event);
            const auto result = WaitForSingleObject(frame_event, INFINITE);
            if(result == WAIT_ABANDONED) {
                logger->error("Waiting for GPU frame %u was abandoned", frame_index);

            } else if(result == WAIT_TIMEOUT) {
                logger->error("Waiting for GPU frame %u timed out", frame_index);

            } else if(result == WAIT_FAILED) {
                logger->error("Waiting for GPU fence %u failed: %s", frame_index, get_last_windows_error());
            }

            RX_ASSERT(result == WAIT_OBJECT_0, "Waiting for frame %u failed", frame_index);
        }
    }

    void RenderBackend::wait_gpu_idle(const uint64_t frame_index) {
        frame_fence_values[frame_index] += 3;
        direct_command_queue->Signal(direct_command_ready_fence, frame_fence_values[frame_index]);
        wait_for_frame(frame_index);
    }

    Buffer RenderBackend::create_staging_buffer(const Uint64 size, const Uint64 alignment) {
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_NONE, alignment);

        const D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;

        D3D12MA::ALLOCATION_DESC alloc_desc{};
        alloc_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

        auto buffer = Buffer{};
        auto result = device_allocator
                          ->CreateResource(&alloc_desc, &desc, initial_state, nullptr, &buffer.allocation, IID_PPV_ARGS(&buffer.resource));
        if(result == DXGI_ERROR_DEVICE_REMOVED) {
            log_dred_report();

            const auto removed_reason = device->GetDeviceRemovedReason();
            logger->error("Device was removed because: %s", to_string(removed_reason));
        }
        if(FAILED(result)) {
            Rx::abort("Could not create staging buffer: %s", to_string(result));
        }

        buffer.size = size;
        buffer.alignment = alignment;
        D3D12_RANGE range{0, size};
        result = buffer.resource->Map(0, &range, &buffer.mapped_ptr);
        if(FAILED(result)) {
            Rx::abort("Could not map staging buffer: %s", to_string(result));
        }

        const auto msg = Rx::String::format("Staging Buffer %d", staging_buffer_idx);
        set_object_name(buffer.resource, msg);
        staging_buffer_idx++;

        return buffer;
    }

    Buffer RenderBackend::create_scratch_buffer(const Uint32 num_bytes) {
        constexpr auto alignment = std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
                                            D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(num_bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, alignment);

        const auto alloc_desc = D3D12MA::ALLOCATION_DESC{.HeapType = D3D12_HEAP_TYPE_DEFAULT};

        auto scratch_buffer = Buffer{};
        const auto result = device_allocator->CreateResource(&alloc_desc,
                                                             &desc,
                                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                             nullptr,
                                                             &scratch_buffer.allocation,
                                                             IID_PPV_ARGS(&scratch_buffer.resource));
        if(FAILED(result)) {
            logger->error("Could not create scratch buffer: %s", to_string(result));
        }
        if(result == DXGI_ERROR_DEVICE_REMOVED) {
            log_dred_report();

            Rx::abort("Device removed when creating a staging buffer of size %u", num_bytes);
        }

        scratch_buffer.size = num_bytes;
        set_object_name(scratch_buffer.resource, Rx::String::format("Scratch buffer %d", scratch_buffer_counter));
        scratch_buffer_counter++;

        return scratch_buffer;
    }

    ComPtr<ID3D12Fence> RenderBackend::get_next_command_list_done_fence() {
        if(!command_list_done_fences.is_empty()) {
            auto fence = command_list_done_fences[command_list_done_fences.size() - 1];
            command_list_done_fences.pop_back();

            return fence;
        }

        ComPtr<ID3D12Fence> fence;
        const auto result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if(FAILED(result)) {
            logger->error("Could not create fence: %s", to_string(result));
            const auto removed_reason = device->GetDeviceRemovedReason();
            logger->error("Device removed reason: %s", to_string(removed_reason));
        }

        return fence;
    }

    void RenderBackend::log_dred_report() const {
        ID3D12DeviceRemovedExtendedData1* dred = nullptr;
        device->QueryInterface(&dred);

        if(!dred) {
            return;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs;
        auto result = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
        if(FAILED(result)) {
            return;
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT1 page_faults;
        result = dred->GetPageFaultAllocationOutput1(&page_faults);
        if(FAILED(result)) {
            return;
        }

        logger->error("Command history:\n%s", breadcrumb_output_to_string(breadcrumbs));
        logger->error(page_fault_output_to_string(page_faults).data());

        logger->flush();
    }

    Rx::Ptr<RenderBackend> make_render_device(GLFWwindow* window) {
        auto hwnd = glfwGetWin32Window(window); // NOLINT(readability-qualified-auto)

        glm::ivec2 framebuffer_size{};
        glfwGetFramebufferSize(window, &framebuffer_size.x, &framebuffer_size.y);

        logger->info("Creating D3D12 backend with framebuffer resolution %dx%d", framebuffer_size.x, framebuffer_size.y);

        return Rx::make_ptr<RenderBackend>(RX_SYSTEM_ALLOCATOR, hwnd, framebuffer_size);
    }

    Rx::String message_category_to_string(const D3D12_MESSAGE_CATEGORY category) {
        switch(category) {
            case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED:
                return "application-defined";

            case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS:
                return "miscellaneous";

            case D3D12_MESSAGE_CATEGORY_INITIALIZATION:
                return "initialization";

            case D3D12_MESSAGE_CATEGORY_CLEANUP:
                return "cleanup";

            case D3D12_MESSAGE_CATEGORY_COMPILATION:
                return "compilation";

            case D3D12_MESSAGE_CATEGORY_STATE_CREATION:
                return "state creation";

            case D3D12_MESSAGE_CATEGORY_STATE_SETTING:
                return "state setting";

            case D3D12_MESSAGE_CATEGORY_STATE_GETTING:
                return "state getting";

            case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION:
                return "resource manipulation";

            case D3D12_MESSAGE_CATEGORY_EXECUTION:
                return "execution";

            case D3D12_MESSAGE_CATEGORY_SHADER:
                return "shader";

            default:
                return "unknown";
        }
    }

    void print_debug_message(const D3D12_MESSAGE_CATEGORY category,
                             const D3D12_MESSAGE_SEVERITY severity,
                             const D3D12_MESSAGE_ID /* id */,
                             const LPCSTR description,
                             void* context) {
        auto* message_logger = static_cast<Rx::Log*>(context);

        const auto category_string = message_category_to_string(category);
        const auto description_wide_string = Rx::WideString{reinterpret_cast<const Uint16*>(description)};
        const auto message = Rx::String::format("%s (Category: %s)", description_wide_string, category_string);

        switch(severity) {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                [[fallthrough]];
            case D3D12_MESSAGE_SEVERITY_ERROR:
                message_logger->error("%s", message);
                break;

            case D3D12_MESSAGE_SEVERITY_WARNING:
                message_logger->warning("%s", message);
                break;

            case D3D12_MESSAGE_SEVERITY_INFO:
                message_logger->info("%s", message);
                break;

            case D3D12_MESSAGE_SEVERITY_MESSAGE:
                message_logger->verbose("%s", message);
                break;

            default:
                message_logger->info("%s", message);
        }
    }
} // namespace sanity::engine::renderer
