#pragma once

#include <d3d12.h>
#include <dxgi.h>

#include "renderer/mesh_data_store.hpp"
#include "renderer/rhi/raytracing_structs.hpp"
#include "renderer/rhi/render_pipeline_state.hpp"
#include "renderer/rhi/resources.hpp"

namespace sanity::engine::renderer {
    class Renderer;
    struct RenderTargetBeginningAccess;
    struct RenderTargetEndingAccess;

    constexpr uint64_t FENCE_UNSIGNALED = 0;
    constexpr uint64_t CPU_FENCE_SIGNALED = 32;
    constexpr uint64_t GPU_FENCE_SIGNALED = 64;
    constexpr Uint32 FRAME_COMPLETE = 128;

    [[nodiscard]] DXGI_FORMAT to_dxgi_format(TextureFormat format);

    [[nodiscard]] D3D12_BLEND to_d3d12_blend(BlendFactor factor);

    [[nodiscard]] D3D12_BLEND_OP to_d3d12_blend_op(BlendOp op);

    [[nodiscard]] D3D12_FILL_MODE to_d3d12_fill_mode(FillMode mode);

    [[nodiscard]] D3D12_CULL_MODE to_d3d12_cull_mode(CullMode mode);

    [[nodiscard]] D3D12_COMPARISON_FUNC to_d3d12_comparison_func(CompareOp op);

    [[nodiscard]] D3D12_STENCIL_OP to_d3d12_stencil_op(StencilOp op);

    [[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY_TYPE to_d3d12_primitive_topology_type(PrimitiveType topology);

    [[nodiscard]] D3D12_RENDER_PASS_BEGINNING_ACCESS to_d3d12_beginning_access(const RenderTargetBeginningAccess& access,
                                                                               bool is_color = true);

    [[nodiscard]] D3D12_RENDER_PASS_ENDING_ACCESS to_d3d12_ending_access(const RenderTargetEndingAccess& access);

    [[nodiscard]] bool can_promote_from_common(D3D12_RESOURCE_STATES states);

    [[nodiscard]] bool can_decay_to_common(D3D12_RESOURCE_STATES states);

    [[nodiscard]] Rx::String resource_state_to_string(D3D12_RESOURCE_STATES state);

    [[nodiscard]] Rx::String breadcrumb_op_to_string(D3D12_AUTO_BREADCRUMB_OP op);

    [[nodiscard]] Rx::String allocation_type_to_string(D3D12_DRED_ALLOCATION_TYPE type);

    [[nodiscard]] Rx::String breadcrumb_output_to_string(const D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1& breadcrumbs);

    [[nodiscard]] Rx::String page_fault_output_to_string(const D3D12_DRED_PAGE_FAULT_OUTPUT1& page_fault_output);

    [[nodiscard]] void upload_data_with_staging_buffer(ID3D12GraphicsCommandList* commands,
                                                       RenderBackend& device,
                                                       ID3D12Resource* dst,
                                                       const void* src,
                                                       Uint32 size,
                                                       Uint32 dst_offset = 0);
} // namespace sanity::engine::renderer
