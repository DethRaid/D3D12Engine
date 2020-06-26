#pragma once

#include <core/types.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <rx/core/ptr.h>
#include <rx/core/vector.h>

#include "resources.hpp"

struct StandardVertex {
    Vec3f position;
    Vec3f normal;
    Uint32 color{0xFFFFFFFF};
    Vec2f texcoord{};
};

namespace renderer {
    class ResourceCommandList;
    class RenderDevice;

    struct Mesh {
        Uint32 first_vertex{0};
        Uint32 num_vertices{0};

        Uint32 first_index{0};
        Uint32 num_indices{0};
    };

    /*!
     * \brief Binding for a vertex buffer
     */
    struct VertexBufferBinding {
        /*!
         * \brief The buffer to bind
         */
        Buffer* buffer;

        /*!
         * \brief Offset in bytes where the relevant data starts
         */
        Uint32 offset;

        /*!
         * \brief Size of a vertex, in bytes
         */
        Uint32 vertex_size;
    };

    class MeshDataStore {
    public:
        MeshDataStore(RenderDevice& device_in, Rx::Ptr<Buffer> vertex_buffer_in, Rx::Ptr<Buffer> index_buffer_in);

        MeshDataStore(const MeshDataStore& other) = delete;
        MeshDataStore& operator=(const MeshDataStore& other) = delete;

        MeshDataStore(MeshDataStore&& old) noexcept = default;
        MeshDataStore& operator=(MeshDataStore&& old) noexcept = delete;

        [[nodiscard]] const Rx::Vector<VertexBufferBinding>& get_vertex_bindings() const;

        [[nodiscard]] const Buffer& get_index_buffer() const;

        /*!
         * \brief Adds new mesh data to the vertex and index buffers. Must be called after `begin_mesh_data_upload` and before
         * `end_mesh_data_upload`
         */
        [[nodiscard]] Mesh add_mesh(const Rx::Vector<StandardVertex>& vertices,
                                    const Rx::Vector<Uint32>& indices,
                                    ID3D11DeviceContext* context);

        void bind_to_context(ID3D11DeviceContext* context) const;

    private:
        RenderDevice* device;

        Rx::Ptr<Buffer> vertex_buffer;

        Rx::Ptr<Buffer> index_buffer;

        Rx::Vector<VertexBufferBinding> vertex_bindings{};

        /*!
         * \brief Index of the byte in the vertex buffer where the next mesh can be uploaded to
         *
         * I'll eventually need a way to unload meshes, but that's more complicated
         */
        Uint32 next_free_vertex_byte{0};

        /*!
         * \brief The offset in the vertex buffer, in vertices, where the next mesh's vertex data should start
         */
        Uint32 next_vertex_offset{0};

        /*!
         * \brief The offset in the index buffer where the next mesh's indices should start
         */
        Uint32 next_index_offset{0};
    };
} // namespace renderer
