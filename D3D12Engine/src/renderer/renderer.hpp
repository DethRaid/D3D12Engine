#pragma once
#include "../rhi/mesh_data_store.hpp"
#include "components.hpp"
#include "entt/entity/registry.hpp"

namespace rhi {
    class RenderCommandList;
    class RenderDevice;
}


struct GLFWwindow;

namespace renderer {
    /*!
     * \brief Renderer class that uses a clustered forward lighting algorithm
     *
     * It won't actually do that for a while, but having a strong name is very useful
     */
    class Renderer {
    public:
        explicit Renderer(GLFWwindow* window);

        void render_scene(entt::registry& registry);

        [[nodiscard]] StaticMeshRenderable create_static_mesh(const std::vector<rhi::BveVertex>& vertices, const std::vector<uint32_t>& indices) const;

    private:
        std::unique_ptr<rhi::RenderDevice> render_device;

        std::unique_ptr<rhi::MeshDataStore> static_mesh_storage;

        void make_static_mesh_storage();

        void render_3d_scene(entt::registry& registry, rhi::RenderCommandList& command_list);
    };
} // namespace renderer
