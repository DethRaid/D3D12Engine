#pragma once

#include "core/transform.hpp"
#include "renderer/handles.hpp"
#include "renderer/mesh.hpp"
#include "resources.hpp"

namespace sanity::engine::renderer {
    constexpr Uint32 OPAQUE_OBJECT_BIT = 0x01;
    constexpr Uint32 TRANSPARENT_OBJECT_BIT = 0x02;
    constexpr Uint32 LIGHT_SOURCE_BIT = 0x10;

    struct PlacedMesh {
        Mesh mesh{};

        glm::mat4 model_matrix{1.f};
    };

    /*!
     * \brief Represents an object that can be raytraced against
     *
     * In general, you should create one of these for each of the meshes in the scene
     */
    struct RaytracingAccelerationStructure {
        /*!
         * \brief Buffer that holds the bottom-level acceleration structure
         */
        BufferHandle blas_buffer{};
    };

	using RaytracingAsHandle = GpuResourceHandle<RaytracingAccelerationStructure>;

    struct RaytracingMaterial {
        Uint32 handle : 24;
    };

    struct RaytracingObject {
        /*!
         * \brief Buffer that holds the object's bottom-level acceleration structure
         */
        RaytracingAsHandle as_handle{};

        /*!
         * \brief Material to render this RaytracingObject with
         */
        RaytracingMaterial material{0};

        glm::mat4 transform{};
    };

    /*!
     * \brief Struct for the top level acceleration structure that we can raytrace against
     */
    struct RaytracingScene {
        BufferHandle buffer{};
    };
} // namespace sanity::engine::renderer
