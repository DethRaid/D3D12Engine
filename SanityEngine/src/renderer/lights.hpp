#pragma once

#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"
#include "glm/vec3.hpp"
#include "renderer/rhi/resources.hpp"

namespace sanity::engine::renderer {
    enum class LightType { Directional = 0, Sphere = 1 };

    /*!
     * @brief Struct for a light on the GPU
     */
    struct GpuLight {
        LightType type{LightType::Directional};

        /*!
         * \brief HDR color of this light
         */
        glm::vec3 color = glm::vec3{254.f / 255.f, 238.f / 255.f, 244.f / 255.f} * 17.f;

        // const float verticalAngle = 5.4789
        // const float horizontalAngle = 2.8651

        /*!
         * @brief If the light is directional, this is the worldspace direction of the light. If the light is a sphere, tube, or rectangular
         * light, this is the worldspace location of the light
         */
        glm::vec3 direction_or_location{glm::normalize(glm::vec3{0.049756793f, 0.59547983f, -0.994187036f})};    // Direction for the test skybox
        // glm::vec3 direction_or_location{glm::normalize(glm::vec3{0.f, 1.f, 0.f})};

        /*!
         * Angular size of the light, in radians. Only relevant for directional lights
         */
        Float32 size{glm::radians(0.53f)};
        // Hack to make the soft shadows easier to see in my test scene, should remove the *10 multiplier when I have a real scene
    };

    using LightHandle = GpuResourceHandle<GpuLight>;

    struct ImageBasedLightingInfo {
        /*!
         * @brief Handle to the texture to use to the skybox that gets drawn directly to screen
         */
        TextureHandle skybox_handle{};

        /*!
         * @brief Handle to the prefiltered environment lighting
         */
        TextureHandle environment_lighting_handle{};

        /*!
         * @brief Handle to the texture to use for reflections
         */
        TextureHandle reflection_map{};
    };
} // namespace sanity::engine::renderer
