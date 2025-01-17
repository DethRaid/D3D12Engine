#include "EditorUiController.hpp"

#include "sanity_engine.hpp"
#include "../../scene_viewport.hpp"
#include "ui/Window.hpp"
#include "windows/EntityEditorWindow.hpp"
#include "windows/WorldgenParamsEditor.hpp"
#include "windows/content_browser.hpp"
#include "windows/mesh_import_window.hpp"
#include "windows/scene_hierarchy.hpp"

namespace sanity::editor::ui {
    EditorUiController::EditorUiController() {
        auto& registry = engine::g_engine->get_entity_registry();
        
        content_browser = create_window_entity<ContentBrowser>(registry);
        content_browser->is_visible = true;

        scene_hierarchy = create_window_entity<SceneHierarchy>(registry, registry, *this);
        scene_hierarchy->is_visible = true;

    	auto& renderer = engine::g_engine->get_renderer();
    	scene_viewport = create_window_entity<SceneViewport>(registry, renderer);
        scene_viewport->is_visible = true;
    }

    void EditorUiController::show_worldgen_params_editor() const { worldgen_params_editor->is_visible = true; }

    EntityEditorWindow* EditorUiController::show_edit_entity_window(const entt::entity entity, entt::registry& registry) const {
        auto* entity_editor_window = create_window_entity<EntityEditorWindow>(registry, entity, registry);

        entity_editor_window->is_visible = true;

        return entity_editor_window;
    }

    void EditorUiController::create_and_edit_new_entity(const engine::ActorType actor_type) const {
        auto& registry = engine::g_engine->get_entity_registry();

        auto& new_actor = create_actor(registry, "New Actor", actor_type);
        
        show_edit_entity_window(new_actor.entity, registry);
    }

    void EditorUiController::set_content_browser_directory(const std::filesystem::path& content_directory) const {
        if(content_browser != nullptr) {
            content_browser->set_content_directory(content_directory);
        }
    }

    void EditorUiController::show_scene_hierarchy_window() const {
        if(scene_hierarchy != nullptr) {
            scene_hierarchy->is_visible = true;
        }
    }

    void EditorUiController::show_editor_for_asset(const std::filesystem::path& asset_path) const {
        const auto extension = asset_path.extension();
        if(extension == ".glb" || extension == ".gltf") {
            open_mesh_import_settings(asset_path);
        }
    }

    void EditorUiController::open_mesh_import_settings(const std::filesystem::path& mesh_path) const {
        auto& registry = engine::g_engine->get_entity_registry();

        auto* window = create_window_entity<SceneImportWindow>(registry, mesh_path);
        window->is_visible = true;
    }
} // namespace sanity::editor::ui
