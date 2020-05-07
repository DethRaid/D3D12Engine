#pragma once

#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace renderer {
    constexpr uint32_t MAX_NUM_LIGHTS = 32;

    struct LightHandle {
        uint32_t handle{0};
    };

    enum class LightType {
        Directional = 0,
    };

    struct Light {
        LightType type{LightType::Directional};

        /*!
         * \brief HDR color of this light
         */
        glm::vec3 color{1, 1, 1};

        glm::vec3 direction{glm::normalize(glm::vec3{-1, -1, -1})};

        /*!
         * Angular size of the light, in radians. Only relevant for directional lights
         */
        float angular_size{glm::radians(0.53f)};
    };
} // namespace renderer