#include "single_pass_downsampler.hpp"

#include "Tracy/Tracy.hpp"
#include "Tracy/TracyD3D12.hpp"
#include "loading/shader_loading.hpp"
#include "rhi/d3d12_private_data.hpp"
#include "rx/core/log.h"
#include "single_pass_downsampler.hpp"

#define A_CPU
#include "ffx_a.h"
#include "ffx_spd.h"

namespace sanity::engine::renderer {
    RX_LOG("SinglePassDownsampler", logger);

    constexpr auto SPD_MAX_MIP_LEVELS = 12;

    SinglePassDownsampler SinglePassDownsampler::Create(RenderBackend& backend) {
        ZoneScoped;

        Rx::Vector<CD3DX12_ROOT_PARAMETER> spd_params;
        spd_params.resize(3);

        // Shader parameter constants
        spd_params[ROOT_CONSTANTS_INDEX].InitAsConstants(6, 0);

        // UAV table + global counter buffer
        spd_params[GLOBAL_COUNTER_BUFFER_INDEX].InitAsUnorderedAccessView(1);

        D3D12_DESCRIPTOR_RANGE ranges[3] = {CD3DX12_DESCRIPTOR_RANGE{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2},
                                            CD3DX12_DESCRIPTOR_RANGE{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, SPD_MAX_MIP_LEVELS + 1, 3},
                                            CD3DX12_DESCRIPTOR_RANGE{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0}};

        spd_params[DESCRIPTOR_TABLE_INDEX].InitAsDescriptorTable(3, ranges);

        auto static_sampler_desc = CD3DX12_STATIC_SAMPLER_DESC{0};
        static_sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        static_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        static_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;

        const auto desc = D3D12_ROOT_SIGNATURE_DESC{.NumParameters = static_cast<UINT>(spd_params.size()),
                                                    .pParameters = spd_params.data(),
                                                    .NumStaticSamplers = 1,
                                                    .pStaticSamplers = &static_sampler_desc};

        const auto spd_root_sig = backend.compile_root_signature(desc);
        set_object_name(spd_root_sig, "SPD Root Signature");

        const auto compute_instructions = load_shader("utility/single_pass_downsampler.compute");

        const auto spd_pipeline = backend.create_compute_pipeline_state(compute_instructions, spd_root_sig);
        set_object_name(spd_pipeline, "SPD Compute Pipeline");

        return SinglePassDownsampler{spd_root_sig, spd_pipeline, backend};
    }

    void SinglePassDownsampler::generate_mip_chain_for_texture(ID3D12Resource* texture, ID3D12GraphicsCommandList2* cmds) const {
        const auto texture_name = get_object_name(texture);
        ZoneScoped;

        TracyD3D12Zone(RenderBackend::tracy_render_context, cmds, "SinglePassDownsampler::generate_mip_chain_for_texture");

        const auto device = backend->device;

        // Initialize SPD parameters
        varAU2(dispatch_thread_group_count_xy);
        varAU2(work_group_offset);
        varAU2(num_work_groups_and_mips);

        const auto desc = texture->GetDesc();
        varAU4(rect_info) = initAU4(0, 0, static_cast<AU1>(desc.Width), desc.Height);
        const auto inverse_size = Vec2f{1.0f / static_cast<Float32>(desc.Width), 1.0f / static_cast<Float32>(desc.Height)};

        SpdSetup(dispatch_thread_group_count_xy, work_group_offset, num_work_groups_and_mips, rect_info);

        const auto num_work_groups = num_work_groups_and_mips[0];
        const auto num_mips = num_work_groups_and_mips[1];

        // Set up descriptors
        const auto descriptor_table_handle = fill_descriptor_table(texture, device, num_mips);

        // Allowed usage of creating a non-bindless buffer, since this uses a bindy resource mode

        // TODO: Convert SPD to use bindless resources

        auto global_counter_buffer = backend->create_buffer(
            {.name = "SPD Global Counter", .usage = BufferUsage::UnorderedAccess, .size = sizeof(Uint32)});

        // Bind descriptor heap, root signature, and pipeline
        auto* descriptor_heap = backend->get_cbv_srv_uav_heap();
        cmds->SetDescriptorHeaps(1, &descriptor_heap);
        cmds->SetComputeRootSignature(root_signature);
        cmds->SetPipelineState(pipeline);

        // Set parameters
        cmds->SetComputeRoot32BitConstant(ROOT_CONSTANTS_INDEX, num_mips, MIP_COUNT_ROOT_CONSTANT_OFFSET);
        cmds->SetComputeRoot32BitConstant(ROOT_CONSTANTS_INDEX, num_work_groups, NUM_WORK_GROUPS_ROOT_CONSTANT_OFFSET);
        cmds->SetComputeRoot32BitConstants(ROOT_CONSTANTS_INDEX, 2, &work_group_offset, OFFSET_ROOT_CONSTANT_OFFSET);
        cmds->SetComputeRoot32BitConstants(ROOT_CONSTANTS_INDEX, 2, &inverse_size, INVERSE_SIZE_ROOT_CONSTANT_OFFSET);

        cmds->SetComputeRootUnorderedAccessView(GLOBAL_COUNTER_BUFFER_INDEX, global_counter_buffer->resource->GetGPUVirtualAddress());

        cmds->SetComputeRootDescriptorTable(DESCRIPTOR_TABLE_INDEX, descriptor_table_handle.gpu_handle);

        // Set counter to 0
        {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(global_counter_buffer->resource,
                                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                      D3D12_RESOURCE_STATE_COPY_DEST,
                                                                      0);
            cmds->ResourceBarrier(1, &barrier);

            D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{.Dest = global_counter_buffer->resource->GetGPUVirtualAddress(), .Value = 0};
            cmds->WriteBufferImmediate(1, &param, nullptr);

            D3D12_RESOURCE_BARRIER barriers[1] = {CD3DX12_RESOURCE_BARRIER::Transition(global_counter_buffer->resource,
                                                                                       D3D12_RESOURCE_STATE_COPY_DEST,
                                                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                                       0)};
            cmds->ResourceBarrier(1, barriers);
        }

        // Transition mip 0 to SRV
        {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture,
                                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                                      0);
            cmds->ResourceBarrier(1, &barrier);
        }

        cmds->Dispatch(dispatch_thread_group_count_xy[0], dispatch_thread_group_count_xy[1], 1);

        // And back to UAV
        {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture,
                                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                                      0);
            cmds->ResourceBarrier(1, &barrier);
        }

        // TODO: Free descriptors

        backend->schedule_buffer_destruction(*global_counter_buffer);
    }

    SinglePassDownsampler::SinglePassDownsampler(ComPtr<ID3D12RootSignature> root_signature_in,
                                                 ComPtr<ID3D12PipelineState> pipeline_in,
                                                 RenderBackend& backend_in)
        : root_signature{Rx::Utility::move(root_signature_in)}, pipeline{Rx::Utility::move(pipeline_in)}, backend{&backend_in} {}

    DescriptorRange SinglePassDownsampler::fill_descriptor_table(ID3D12Resource* texture,
                                                                 ID3D12Device* device,
                                                                 const Uint32 num_mips) const {
        const auto& desc = texture->GetDesc();

        auto& descriptor_allocator = backend->get_cbv_srv_uav_allocator();

        const auto descriptor_size = descriptor_allocator.get_descriptor_size();
        const auto output_mips_descriptors = descriptor_allocator.allocate_descriptors(16);
        const auto first_cpu_descriptor_ptr = output_mips_descriptors.cpu_handle.ptr;

        auto cur_descriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE{output_mips_descriptors.cpu_handle};

        const auto mip_6_actual_slice = std::min(6u, num_mips);
        auto mip_6_uav_desc = D3D12_UNORDERED_ACCESS_VIEW_DESC{.Format = desc.Format,
                                                               .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                                                               .Texture2D = {.MipSlice = mip_6_actual_slice, .PlaneSlice = 0}};
        if(mip_6_uav_desc.Format == DXGI_FORMAT_R32_TYPELESS) {
            mip_6_uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
        }

        device->CreateUnorderedAccessView(texture, nullptr, &mip_6_uav_desc, cur_descriptor);

        cur_descriptor = cur_descriptor.Offset(1, descriptor_size);

        for(Uint32 i = 0; i < num_mips; i++) {
            auto uav_desc = D3D12_UNORDERED_ACCESS_VIEW_DESC{.Format = desc.Format,
                                                             .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                                                             .Texture2D = {.MipSlice = i + 1, .PlaneSlice = 0}};
            if(uav_desc.Format == DXGI_FORMAT_R32_TYPELESS) {
                uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
            }

            device->CreateUnorderedAccessView(texture, nullptr, &uav_desc, cur_descriptor);

            cur_descriptor = cur_descriptor.Offset(1, descriptor_size);
        }

        cur_descriptor = cur_descriptor.Offset(SPD_MAX_MIP_LEVELS + 1 - num_mips, descriptor_size);

        auto srv_desc = D3D12_SHADER_RESOURCE_VIEW_DESC{.Format = desc.Format,
                                                        .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                                        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                        .Texture2D = {.MostDetailedMip = 0,
                                                                      .MipLevels = 1,
                                                                      .PlaneSlice = 0,
                                                                      .ResourceMinLODClamp = 0}};
        if(srv_desc.Format == DXGI_FORMAT_R32_TYPELESS) {
            srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        }
        device->CreateShaderResourceView(texture, &srv_desc, cur_descriptor);

        return output_mips_descriptors;
    }
} // namespace sanity::engine::renderer
