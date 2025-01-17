#pragma once

#include "renderer/render_pass.hpp"

namespace sanity::engine::renderer {
    /*!
     * \brief Sorts lights into frustum-aligned clusters
     */
    class LightClusterPass : public RenderPass {
    public:
        explicit LightClusterPass() = default;

        ~LightClusterPass() override = default;
    };
} // namespace renderer
