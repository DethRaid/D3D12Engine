#pragma once

#include "renderer/hlsl/fluid_sim.hpp"
#include "renderer/render_pass.hpp"
#include "renderer/rhi/compute_pipeline_state.hpp"
#include "renderer/rhi/per_frame_buffer.hpp"
#include "renderer/rhi/render_backend.hpp"

namespace sanity::engine::renderer {
    class Renderer;

    /**
     * @brief Indirect dispatch command for executing a single fluid sim dispatch
     *
     * All the different steps of the fluid simulation use the same parameters, so using the same struct for them isn't a problem
     */
    struct FluidSimDispatchCommand {
        uint data_idx;
        uint model_matrix_idx;
        uint entity_id;

        uint thread_group_count_x;
        uint thread_group_count_y;
        uint thread_group_count_z;
    };

    struct FluidSimDrawCommand {
        uint data_idx;
        uint model_matrix_idx;
        uint entity_id;

        uint index_count;
        uint instance_count;
        uint first_index;
        uint first_vertex;
        uint first_instance;
    };

    static_assert(sizeof(FluidSimDispatchCommand) == sizeof(uint) * 6);
    static_assert(sizeof(FluidSimDrawCommand) == sizeof(uint) * 8);

    /**
     * @brief Executes all fluid simulations, including fire, smoke, and water
     */
    class FluidSimPass final : public RenderPass {
    public:
        explicit FluidSimPass(Renderer& renderer_in, const glm::uvec2& render_resolution);

        void prepare_work(entt::registry& registry, Uint32 frame_idx, float delta_time) override;

        void record_work(ID3D12GraphicsCommandList4* commands, entt::registry& registry, Uint32 frame_idx, float delta_time) override;

        [[nodiscard]] TextureHandle get_color_target_handle() const;

    private:
        Renderer* renderer{nullptr};

        TextureHandle fluid_color_texture;
        DescriptorRange fluid_color_rtv;

        /**
         * @brief All the fluid volumes we're updating this frame
         */
        Rx::Vector<FluidVolumeHandle> active_fluid_volumes;

        /**
         * @brief Tracks the state of read/write textures for each active fluid volume
         */
        Rx::Vector<GpuFluidVolumeState> fluid_volume_states;

        BufferRing advection_params_array;
        BufferRing buoyancy_params_array;
        BufferRing emitters_params_array;
        BufferRing extinguishment_params_array;
        BufferRing vorticity_confinement_params_array;
        BufferRing divergence_params_array;
        Rx::Vector<BufferRing> pressure_param_arrays;
        BufferRing projection_param_arrays;

        BufferRing rendering_params_array;

        ComPtr<ID3D12PipelineState> advection_pipeline;
        ComPtr<ID3D12PipelineState> buoyancy_pipeline;
        ComPtr<ID3D12PipelineState> emitters_pipeline;
        ComPtr<ID3D12PipelineState> extinguishment_pipeline;
        ComPtr<ID3D12PipelineState> vorticity_pipeline;
        ComPtr<ID3D12PipelineState> confinement_pipeline;
        ComPtr<ID3D12PipelineState> divergence_pipeline;
        ComPtr<ID3D12PipelineState> jacobi_pressure_solver_pipeline;
        ComPtr<ID3D12PipelineState> projection_pipeline;

        ComPtr<ID3D12CommandSignature> fluid_sim_dispatch_signature;
        Rx::Vector<FluidSimDispatchCommand> fluid_sim_dispatches;
        BufferRing fluid_sim_dispatch_command_buffers;

        /**
         * @brief Unit cube with the origin in the middle of the bottom face
         */
        BufferHandle cube_vertex_buffer;
        BufferHandle cube_index_buffer;
        Rx::Ptr<RenderPipelineState> fire_fluid_pipeline;
        ComPtr<ID3D12CommandSignature> fluid_volume_draw_signature;
        Rx::Vector<FluidSimDrawCommand> fluid_sim_draws;
        BufferRing drawcalls;
        D3D12_RENDER_PASS_RENDER_TARGET_DESC fluid_target_access;
        D3D12_RENDER_PASS_DEPTH_STENCIL_DESC depth_access;

        // init

        void record_fire_simulation_updates(ID3D12GraphicsCommandList* commands, Uint32 frame_idx);

        void advance_fire_sim_params_arrays();

        void create_pipelines();

        void create_simulation_pipelines();

        void create_render_pipelines();

        void create_indirect_command_signatures();

        void create_render_target(const glm::uvec2& render_resolution);

        void create_fluid_volume_geometry();

        void set_resource_states();

        // runtime

        void add_fluid_volume_dispatch(const FluidVolume& fluid_volume, const ObjectDrawData& instance_data);

        void add_fluid_volume_draw(const FluidVolume& fluid_volume, const ObjectDrawData& instance_data);

        void add_fluid_volume_state(const FluidVolume& fluid_volume);

        void set_buffer_indices(ID3D12GraphicsCommandList* commands, Uint32 frame_idx) const;

        void execute_simulation_step(
            ID3D12GraphicsCommandList* commands,
            const BufferRing& data_buffer,
            const ComPtr<ID3D12PipelineState>& pipeline,
            Rx::Function<void(GpuFluidVolumeState&, Rx::Vector<D3D12_RESOURCE_BARRIER>& barriers)> synchronize_volume);

        void apply_advection(ID3D12GraphicsCommandList* commands);

        void apply_buoyancy(ID3D12GraphicsCommandList* commands);

        void apply_emitters(ID3D12GraphicsCommandList* commands);

        void apply_extinguishment(ID3D12GraphicsCommandList* commands);

        void compute_vorticity_confinement(ID3D12GraphicsCommandList* commands);

        void compute_divergence(ID3D12GraphicsCommandList* commands);

        void compute_pressure(ID3D12GraphicsCommandList* commands);

        void compute_projection(ID3D12GraphicsCommandList* commands);

        void barrier_and_swap(Uint32 handles[2], Rx::Vector<D3D12_RESOURCE_BARRIER>& barriers) const;

        struct TextureCopyParams {
            D3D12_TEXTURE_COPY_LOCATION source;
            D3D12_TEXTURE_COPY_LOCATION dest;
        };

        void copy_read_texture_to_write_texture(TextureHandle read,
                                                TextureHandle write,
                                                Rx::Vector<D3D12_RESOURCE_BARRIER>& pre_copy_barriers,
                                                Rx::Vector<TextureCopyParams>& copies,
                                                Rx::Vector<D3D12_RESOURCE_BARRIER>& post_copy_barriers) const;

        void finalize_resources(ID3D12GraphicsCommandList* commands);
    };
} // namespace sanity::engine::renderer
