#pragma once

#include "core/types.hpp"
#include "glm/fwd.hpp"
#include "glm/vec2.hpp"
#include "renderer/debugging/pix.hpp"
#include "renderer/handles.hpp"
#include "renderer/render_pass.hpp"
#include "renderer/rhi/descriptor_allocator.hpp"
#include "renderer/rhi/framebuffer.hpp"
#include "renderer/rhi/render_pipeline_state.hpp"
#include "rx/core/ptr.h"

namespace sanity::engine::renderer {
    struct BindGroup;
    class RenderBackend;
    class Renderer;

    class DirectLightingPass final : public RenderPass {
    public:
        explicit DirectLightingPass(Renderer& renderer_in, const glm::uvec2& render_resolution);

        ~DirectLightingPass() override;

#pragma region RenderPass
        void prepare_work(entt::registry& registry, Uint32 frame_idx, float delta_time) override;

        void record_work(ID3D12GraphicsCommandList4* commands, entt::registry& registry, Uint32 frame_idx, float delta_time) override;
#pragma endregion

        [[nodiscard]] TextureHandle get_color_target_handle() const;

        [[nodiscard]] TextureHandle get_object_id_texture() const;

        [[nodiscard]] TextureHandle get_depth_target_handle() const;

    private:
        Renderer* renderer;

        Rx::Ptr<RenderPipelineState> standard_pipeline;
        Rx::Ptr<RenderPipelineState> outline_pipeline;
        Rx::Ptr<RenderPipelineState> atmospheric_sky_pipeline;

        TextureHandle color_target_handle;
        TextureHandle object_id_target_handle;
        TextureHandle depth_target_handle;

        TextureHandle downsampled_depth_target_handle;

        Uint64 forward_pass_color{PIX_COLOR(224, 96, 54)};

        D3D12_RENDER_PASS_RENDER_TARGET_DESC color_target_access{};

        D3D12_RENDER_PASS_RENDER_TARGET_DESC object_id_target_access{};

        D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_target_access{};

        glm::uvec2 render_target_size{};

        DescriptorRange color_target_descriptor{};
        DescriptorRange object_id_target_descriptor{};
        DescriptorRange depth_target_descriptor{};

        void create_framebuffer(const glm::uvec2& render_resolution);

        void begin_render_pass(ID3D12GraphicsCommandList4* commands) const;

        void draw_objects_in_scene(ID3D12GraphicsCommandList4* commands, entt::registry& registry, Uint32 frame_idx);

        void draw_outlines(ID3D12GraphicsCommandList4* commands, entt::registry& registry, Uint32 frame_idx);

        void draw_atmosphere(ID3D12GraphicsCommandList4* commands, entt::registry& registry) const;

        void copy_render_targets(ID3D12GraphicsCommandList4* commands) const;
    };
} // namespace sanity::engine::renderer
