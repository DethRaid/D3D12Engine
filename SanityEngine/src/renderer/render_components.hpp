#pragma once

#include "core/types.hpp"
#include "renderer/handles.hpp"
#include "renderer/hlsl/fluid_sim.hpp"
#include "renderer/hlsl/standard_material.hpp"
#include "renderer/lights.hpp"
#include "renderer/mesh.hpp"
#include "renderer/rhi/raytracing_structs.hpp"

namespace sanity::engine::renderer {
    /*!
     * \brief Renders a static mesh with some material
     */
    struct __declspec(uuid("{74AA51B6-38C8-4D49-8A3C-C03BD56E2020}")) StandardRenderableComponent {
        /*!
         * \brief What type of object we're dealing with
         */
        enum class Type {
            /*!
             * \brief This object is in the foreground, and it's opaque. These objects should be drawn first
             */
            ForegroundOpaque = 100,

            /*!
             * \brief This object is in the background of the scene. These objects should be drawn after all opaque foreground objects
             */
            Background = 200,

            /*!
             * \brief This object is in the foreground, and it is transparent
             */
            ForegroundTransparent = 300,
        };

        /*!
         * \brief Mesh to render
         */
        Mesh mesh;

        /*!
         * \brief Material to use when rendering this mesh
         */
        StandardMaterialHandle material{};

        /*!
         * \brief If true this object is rendered in the scene's background layer
         */
        bool is_background{false};
    };

    /**
     * @brief Marks that an object should have an outline drawn around it
     */
    struct __declspec(uuid("{00988F57-AFBD-4E37-9FC8-32813E1F6C2B}")) OutlineRenderComponent {
        /**
         * @brief Scale of the outline mesh compared to the normal mesh
         */
        float outline_scale{1.05f};

        /**
         * @brief Color of the outline
         */
        Vec3f color;

        StandardMaterialHandle material{};
    };

    /*!
     * \brief Renders a postprocessing pass
     */
    struct __declspec(uuid("{3F869FC4-F339-4125-82F2-0A3775552112}")) PostProcessingPassComponent {
        Uint32 draw_idx{0};
        StandardMaterialHandle material{};
    };

    struct __declspec(uuid("{BB1E8A88-79FE-4934-8335-E5226022F441}")) RaytracingObjectComponent {
        RaytracingAsHandle as_handle{};
    };

    /*!
     * \brief Sets up a camera to render with
     */
    struct __declspec(uuid("{23C1D6E0-B8E4-453A-8613-FE2EA86D2631}")) CameraComponent {
        Uint32 idx{};

        double fov{60};
        double aspect_ratio{16.0 / 9.0};
        double near_clip_plane{0.01};

        /*!
         * \brief Width of the camera frustum, in local space
         *
         * Only relevant if `fov` is 0
         */
        double orthographic_size = 100;
    };

    /*!
     * \brief A light that can illuminate the scene
     */
    struct __declspec(uuid("{C1299481-3F19-4068-9724-FD89FF59EA65}")) LightComponent {
        LightHandle handle{};

        LightType type{LightType::Directional};

        /*!
         * \brief HDR color of this light
         */
        glm::vec3 color = glm::vec3{254.f / 255.f, 238.f / 255.f, 244.f / 255.f} * 17.f;

        /*!
         * @brief If the light is directional, this is the angular size of the light. If it's a sphere light, this is the radius of the
         * sphere
         */
        Float32 size{glm::radians(0.53f)};
    };

    /*!
     * \brief Renders a skybox
     *
     * NOTE: Only one allowed in the scene ever
     */
    struct __declspec(uuid("{31AB3022-C3A9-4E48-AC49-2703C66A91EA}")) SkyComponent {
        /**
         * @brief Handle to the skybox texture
         *
         * If this handle is invalid, Sanity will instead render a procedural atmospheric sky
         */
        TextureHandle skybox_texture{};
    };

    struct __declspec(uuid("{6763FAED-5C17-40E1-871F-0115E60F21EA}")) FluidVolumeComponent {
        FluidVolumeHandle volume{};
    	
        glm::uvec3 size;
    };

    void draw_component_properties(StandardRenderableComponent& renderable);

    void draw_component_properties(PostProcessingPassComponent& post_processing);

    void draw_component_properties(RaytracingObjectComponent& raytracing_object);

    void draw_component_properties(CameraComponent& camera);

    void draw_component_properties(LightComponent& light);

    void draw_component_properties(SkyComponent& sky);

	void draw_component_properties(FluidVolumeComponent& volume);
} // namespace sanity::engine::renderer
