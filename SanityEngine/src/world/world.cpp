#include "world.hpp"

#include "actor/actor.hpp"
#include "entt/entity/registry.hpp"
#include "loading/image_loading.hpp"
#include "renderer/render_components.hpp"
#include "rx/core/log.h"
#include "sanity_engine.hpp"
#include "renderer/renderer.hpp"

namespace sanity::engine {
    RX_LOG("World", logger);

    World::World(entt::registry& registry_in) : registry{&registry_in} {
       
    }

    void World::create_planetary_sky(renderer::Renderer& renderer)
    {
	  auto& sky_actor = engine::create_actor(*registry, "Sky");
        sky_actor.add_component<renderer::SkyComponent>();
        auto& sun = sky_actor.add_component<renderer::LightComponent>();
    	sun.handle = renderer.next_next_free_light_handle();
        auto& transform = sky_actor.get_transform();
        transform.rotation = quatLookAtLH(normalize(glm::vec3{0.049756793f, 0.59547983f, -0.994187036f}), glm::vec3{0, 1, 0});
        sky = sky_actor.entity;   
    }

    void World::set_skybox(const std::filesystem::path& skybox_image_path) {
        auto& atmosphere = registry->get<renderer::SkyComponent>(sky);

        // TODO: Load skybox through a AssetStreamingManager 

        if(const auto* skybox_handle = cached_skybox_handles.find(skybox_image_path); skybox_handle != nullptr) {
            atmosphere.skybox_texture = *skybox_handle;
            logger->verbose("Using existing texture %d for skybox image %s", skybox_handle->index, skybox_image_path);

        } else {
            auto& renderer = g_engine->get_renderer();
            const auto handle = load_texture_to_gpu(skybox_image_path, renderer);
            if(!handle) {
                return;
            }

            logger->verbose("Uploaded texture %d for skybox image %s", handle->index, skybox_image_path);
            cached_skybox_handles.insert(skybox_image_path, *handle);

            atmosphere.skybox_texture = *handle;
        }
    }

    Actor& World::create_actor(const Rx::String& name) const {
        auto& actor = engine::create_actor(*registry, name);

        return actor;
    }

    Actor World::get_actor(const entt::entity& entity) { return registry->get<Actor>(entity); }
} // namespace sanity::engine
