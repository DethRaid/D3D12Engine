#include "mesh_data_store.hpp"

#include "Tracy.hpp"
#include "TracyD3D12.hpp"
#include "pix3.h"
#include "renderer.hpp"
#include "renderer/rhi/helpers.hpp"
#include "renderer/rhi/render_backend.hpp"
#include "rx/core/log.h"

namespace sanity::engine::renderer {
    RX_LOG("MeshDataStore", logger);

    MeshUploader::MeshUploader(ID3D12GraphicsCommandList4* cmds_in, MeshDataStore* mesh_store_in)
        : cmds{cmds_in}, mesh_store{mesh_store_in} {
        const auto& index_buffer = mesh_store->get_index_buffer();
        const auto& vertex_buffer = mesh_store->get_vertex_buffer();

        auto* vertex_resource = *vertex_buffer.resource;
        auto* index_resource = *index_buffer.resource;

        Rx::Vector<D3D12_RESOURCE_BARRIER> barriers{2};
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(vertex_resource,
                                                           D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                                           D3D12_RESOURCE_STATE_COPY_DEST);
        barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(index_resource,
                                                           D3D12_RESOURCE_STATE_INDEX_BUFFER,
                                                           D3D12_RESOURCE_STATE_COPY_DEST);

        cmds->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }

    MeshUploader::MeshUploader(MeshUploader&& old) noexcept : cmds{old.cmds}, mesh_store{old.mesh_store} { old.state = State::Empty; }

    MeshUploader& MeshUploader::operator=(MeshUploader&& old) noexcept {
        cmds = old.cmds;
        mesh_store = old.mesh_store;
        state = State::Empty;

        return *this;
    }

    MeshUploader::~MeshUploader() {
        if(state != State::Empty) {
            const auto& index_buffer = mesh_store->get_index_buffer();
            const auto& vertex_buffer = mesh_store->get_vertex_buffer();

            auto* vertex_resource = *vertex_buffer.resource;
            auto* index_resource = *index_buffer.resource;

            auto previous_resource_state = D3D12_RESOURCE_STATE_COPY_DEST;
            if(state == State::BuildRaytracingGeometry) {
                previous_resource_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }

            Rx::Vector<D3D12_RESOURCE_BARRIER> barriers{2};
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(vertex_resource,
                                                               previous_resource_state,
                                                               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
            barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(index_resource, previous_resource_state, D3D12_RESOURCE_STATE_INDEX_BUFFER);

            cmds->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

            state = State::Empty;
        }
    }

    Mesh MeshUploader::add_mesh(const Rx::Vector<StandardVertex>& vertices, const Rx::Vector<Uint32>& indices) const {
        if(state == State::AddVerticesAndIndices) {
            return mesh_store->add_mesh(vertices, indices, cmds);

        } else {
            logger->error("MeshUploader not in the right state to add meshes");
            return {};
        }
    }

    void MeshUploader::prepare_for_raytracing_geometry_build() {
        if(state == State::AddVerticesAndIndices) {
            const auto& index_buffer = mesh_store->get_index_buffer();
            const auto& vertex_buffer = mesh_store->get_vertex_buffer();

            auto* vertex_resource = *vertex_buffer.resource;
            auto* index_resource = *index_buffer.resource;

            Rx::Vector<D3D12_RESOURCE_BARRIER> barriers{2};
            barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(vertex_resource,
                                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(index_resource,
                                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            cmds->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

            state = State::BuildRaytracingGeometry;
        }
    }

    MeshDataStore::MeshDataStore(Renderer& renderer_in, BufferHandle vertex_buffer_in, BufferHandle index_buffer_in)
        : renderer{&renderer_in},
          vertex_buffer_handle{Rx::Utility::move(vertex_buffer_in)},
          index_buffer_handle{Rx::Utility::move(index_buffer_in)} {
        const auto& vertex_buffer = renderer->get_buffer(vertex_buffer_handle);

        vertex_bindings = Rx::Array{VertexBufferBinding{.buffer = *vertex_buffer,
                                                        .offset = offsetof(StandardVertex, location),
                                                        .vertex_size = sizeof(StandardVertex)},
                                    VertexBufferBinding{.buffer = *vertex_buffer,
                                                        .offset = offsetof(StandardVertex, normal),
                                                        .vertex_size = sizeof(StandardVertex)},
                                    VertexBufferBinding{.buffer = *vertex_buffer,
                                                        .offset = offsetof(StandardVertex, color),
                                                        .vertex_size = sizeof(StandardVertex)},
                                    VertexBufferBinding{.buffer = *vertex_buffer,
                                                        .offset = offsetof(StandardVertex, texcoord),
                                                        .vertex_size = sizeof(StandardVertex)}};
    }

    MeshDataStore::~MeshDataStore() {
        auto& backend = renderer->get_render_backend();

        const auto& vertex_buffer = renderer->get_buffer(vertex_buffer_handle);
        const auto& index_buffer = renderer->get_buffer(index_buffer_handle);

        backend.schedule_buffer_destruction(*vertex_buffer);
        backend.schedule_buffer_destruction(*index_buffer);
    }

    BufferHandle MeshDataStore::get_vertex_buffer_handle() const { return vertex_buffer_handle; }

    BufferHandle MeshDataStore::get_index_buffer_handle() const { return index_buffer_handle; }

    Buffer MeshDataStore::get_vertex_buffer() const { return *renderer->get_buffer(vertex_buffer_handle); }

    Buffer MeshDataStore::get_index_buffer() const { return *renderer->get_buffer(index_buffer_handle); }

    MeshUploader MeshDataStore::begin_adding_meshes(ID3D12GraphicsCommandList4* commands) { return MeshUploader{commands, this}; }

    Mesh MeshDataStore::add_mesh(const Rx::Vector<StandardVertex>& vertices,
                                 const Rx::Vector<Uint32>& indices,
                                 ID3D12GraphicsCommandList4* commands) {
        ZoneScoped;

        TracyD3D12Zone(RenderBackend::tracy_render_context, commands, "MeshDataStore::add_mesh");
        PIXScopedEvent(commands, PIX_COLOR_DEFAULT, "MeshDataStore::add_mesh");

        logger->verbose("Adding mesh with %u vertices and %u indices", vertices.size(), indices.size());

        auto& backend = renderer->get_render_backend();

        const auto vertex_data_size = static_cast<Uint32>(vertices.size() * sizeof(StandardVertex));
        const auto index_data_size = static_cast<Uint32>(indices.size() * sizeof(Uint32));

        // Offset the indices so they'll refer to the right vertex
        Rx::Vector<Uint32> offset_indices;
        offset_indices.reserve(indices.size());

        logger->verbose("Offsetting indices by %d", next_vertex_offset);

        indices.each_fwd([&](const Uint32 idx) { offset_indices.push_back(idx + next_vertex_offset); });

        const auto& vertex_buffer = get_vertex_buffer();
        const auto& index_buffer = get_index_buffer();

        auto* vertex_resource = *vertex_buffer.resource;
        auto* index_resource = *index_buffer.resource;

        const auto index_buffer_byte_offset = static_cast<Uint32>(next_index_offset * sizeof(Uint32));

        upload_data_with_staging_buffer(commands, backend, vertex_resource, vertices.data(), vertex_data_size, next_free_vertex_byte);

        upload_data_with_staging_buffer(commands,
                                        backend,
                                        index_resource,
                                        offset_indices.data(),
                                        index_data_size,
                                        index_buffer_byte_offset);

        const auto vertex_offset = static_cast<Uint32>(next_free_vertex_byte / sizeof(StandardVertex));

        next_free_vertex_byte += vertex_data_size;

        const auto index_offset = next_index_offset;

        next_vertex_offset += static_cast<Uint32>(vertices.size());
        next_index_offset += static_cast<Uint32>(indices.size());

        return {.first_vertex = vertex_offset,
                .num_vertices = static_cast<Uint32>(vertices.size()),
                .first_index = index_offset,
                .num_indices = static_cast<Uint32>(indices.size())};
    }

    void MeshDataStore::bind_to_command_list(ID3D12GraphicsCommandList* commands) const {
        // If we have more than 16 vertex attributes, we probably have bigger problems
        Rx::Array<D3D12_VERTEX_BUFFER_VIEW[16]> vertex_buffer_views{};
        for(Uint32 i = 0; i < vertex_bindings.size(); i++) {
            const auto& binding = vertex_bindings[i];
            const auto& buffer = static_cast<const Buffer>(binding.buffer);

            const auto view = D3D12_VERTEX_BUFFER_VIEW{.BufferLocation = buffer.resource->GetGPUVirtualAddress() + binding.offset,
                                                       .SizeInBytes = static_cast<Uint32>(buffer.size - binding.offset),
                                                       .StrideInBytes = binding.vertex_size};

            vertex_buffer_views[i] = view;
        }

        auto* views = vertex_buffer_views.data();
        commands->IASetVertexBuffers(0, static_cast<UINT>(vertex_bindings.size()), views);

        const auto& index_buffer = get_index_buffer();

        const D3D12_INDEX_BUFFER_VIEW index_view{.BufferLocation = index_buffer.resource->GetGPUVirtualAddress(),
                                                 .SizeInBytes = static_cast<Uint32>(index_buffer.size),
                                                 .Format = DXGI_FORMAT_R32_UINT};

        commands->IASetIndexBuffer(&index_view);

        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
} // namespace sanity::engine::renderer
