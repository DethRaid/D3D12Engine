#include "EntityEditorWindow.hpp"

#include "actor/actor.hpp"
#include "core/components.hpp"
#include "entity/Components.hpp"
#include "renderer/render_components.hpp"
#include "sanity_engine.hpp"

using namespace sanity::engine::ui;

namespace sanity::editor::ui {
    void draw_component_editor(const GUID& component_type_id, const entt::entity& entity, entt::registry& registry);

    EntityEditorWindow::EntityEditorWindow(const entt::entity entity_in, entt::registry& registry_in)
        : Window{"Entity Editor"}, entity{entity_in}, registry{&registry_in} {
        const auto& sanity_component = registry->get<engine::Actor>(entity);
        if(!sanity_component.name.is_empty()) {
            name = sanity_component.name;
        }
    }

    void EntityEditorWindow::set_entity(const entt::entity new_entity, entt::registry& new_registry) {
        if(entity != new_entity || registry != &new_registry) {
            entity = new_entity;
            registry = &new_registry;

            const auto& sanity_component = registry->get<engine::Actor>(entity);
            if(!sanity_component.name.is_empty()) {
                name = sanity_component.name;
            }
        }
    }

    void EntityEditorWindow::draw_contents() {
        const auto sanity_entity = registry->get<engine::Actor>(entity);
        sanity_entity.component_class_ids.each_fwd([&](const GUID class_id) { draw_component_editor(class_id, entity, *registry); });
    }

#define DRAW_COMPONENT_EDITOR(Type)                                                                                                        \
    if(component_type_id == _uuidof(Type)) {                                                                                               \
        auto& component = registry.get<Type>(entity);                                                                                      \
        draw_component_editor(component);                                                                                                  \
    }

    void draw_component_editor(const GUID& component_type_id, const entt::entity& entity, entt::registry& registry) {
        const auto class_name = engine::g_engine->get_type_reflector().get_name_of_type(component_type_id);
        // @formatter:off
        DRAW_COMPONENT_EDITOR(engine::Actor)
        else DRAW_COMPONENT_EDITOR(engine::TransformComponent)
        else DRAW_COMPONENT_EDITOR(engine::renderer::StandardRenderableComponent)
        else DRAW_COMPONENT_EDITOR(engine::renderer::PostProcessingPassComponent)
        else DRAW_COMPONENT_EDITOR(engine::renderer::RaytracingObjectComponent)
        else DRAW_COMPONENT_EDITOR(engine::renderer::CameraComponent)
        else DRAW_COMPONENT_EDITOR(engine::renderer::LightComponent)
    	else DRAW_COMPONENT_EDITOR(engine::renderer::SkyComponent);
        // @formatter:on            
    }
} // namespace sanity::editor::ui
