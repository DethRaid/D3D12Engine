#include "render_pass.hpp"

#include "renderer/rhi/d3dx12.hpp"
#include "rx/core/log.h"

RX_LOG("RenderPass", logger);

namespace sanity::engine::renderer {
    void RenderPass::prepare_work(entt::registry& registry, const Uint32 frame_idx, const float delta_time) {
        // Default empty implementation so I don't have to change my existing render passes... yet
    }

    const Rx::Map<TextureHandle, Rx::Optional<BeginEndState>>& RenderPass::get_texture_states() const { return texture_states; }

    const Rx::Map<BufferHandle, Rx::Optional<BeginEndState>>& RenderPass::get_buffer_states() const { return buffer_states; }

    void RenderPass::set_resource_usage(const TextureHandle handle, const D3D12_RESOURCE_STATES states) {
        set_resource_usage(handle, states, states);
    }

    void RenderPass::set_resource_usages(const Rx::Vector<TextureHandle>& handles, const D3D12_RESOURCE_STATES states) {
        handles.each_fwd([&](const TextureHandle& handle) { set_resource_usage(handle, states); });
    }

    void RenderPass::set_resource_usage(const BufferHandle& handle, const D3D12_RESOURCE_STATES states) {
        set_resource_usage(handle, states, states);
    }

    void RenderPass::set_resource_usage(const BufferHandle& handle,
                                        const D3D12_RESOURCE_STATES begin_states,
                                        const D3D12_RESOURCE_STATES end_states) {
        if(auto* usage_ptr = buffer_states.find(handle); usage_ptr != nullptr) {
            *usage_ptr = Rx::Pair{begin_states, end_states};
            return;
        }

        buffer_states.insert(handle, Rx::Pair{begin_states, end_states});
    }

    void RenderPass::set_resource_usage(const Rx::Vector<BufferHandle>& handles, const D3D12_RESOURCE_STATES states) {
        handles.each_fwd([&](const BufferHandle& handle) { set_resource_usage(handle, states); });
    }

    void RenderPass::set_resource_usage(const TextureHandle handle,
                                        const D3D12_RESOURCE_STATES begin_states,
                                        const D3D12_RESOURCE_STATES end_states) {
        if(auto* usage_ptr = texture_states.find(handle); usage_ptr != nullptr) {
            *usage_ptr = Rx::Pair{begin_states, end_states};
            return;
        }

        texture_states.insert(handle, Rx::Pair{begin_states, end_states});
    }

    void RenderPass::set_resource_usages(const Rx::Vector<TextureHandle>& handles,
                                         const D3D12_RESOURCE_STATES begin_states,
                                         const D3D12_RESOURCE_STATES end_states) {
        handles.each_fwd([&](const TextureHandle& handle) { set_resource_usage(handle, begin_states, end_states); });
    }

    void RenderPass::clear_resource_usage(const TextureHandle handle) {
        // Why set the texture's states to an empty optional, rather than simply remove it from the map?
        // Rex maps do not like it when you constantly add and remove the same things from them. I've run into bugs from it before. This
        // way, we never remove things from the map There is a potential memory usage problem... Perhaps we can compact the maps every so
        // often
        if(auto* usage_ptr = texture_states.find(handle); usage_ptr != nullptr) {
            *usage_ptr = Rx::nullopt;
        }
    }
} // namespace sanity::engine::renderer
