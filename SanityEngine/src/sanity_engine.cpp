﻿#include "sanity_engine.hpp"

#include <filesystem>
#include <ranges>

#include "GLFW/glfw3.h"
#include "TracyD3D12.hpp"
#include "actor/actor.hpp"
#include "adapters/rex/rex_wrapper.hpp"
#include "adapters/tracy.hpp"
#include "glm/ext/quaternion_trigonometric.hpp"
#include "renderer/rhi/render_backend.hpp"
#include "rx/core/abort.h"
#include "rx/core/log.h"
#include "stb_image.h"
#include "ui/ConsoleWindow.hpp"
#include "ui/fps_display.hpp"
#include "ui/ui_components.hpp"
#include "world/world.hpp"

namespace sanity::engine {
    static Rx::GlobalGroup s_sanity_engine_globals{"SanityEngine"};

    RX_LOG("SanityEngine", logger);

    RX_CONSOLE_FVAR(simulation_timestep, "Timestep", "Timestep of SanityEngine's simulation, in seconds", 0.0f, 1.0f, 0.0069f);

    RX_CONSOLE_BVAR(show_frametime_display,
                    "Debug.ShowFramerateWindow",
                    "Show the Dear ImGUI window that displays SanityEngine's render framerate",
                    false);

    RX_CONSOLE_BVAR(show_console, "ShowConsole", "Show the SanityEngine command console", true);
    RX_CONSOLE_SVAR(cvar_ini_file_name, "Console.IniFileName", "Filename of the file to read console variables from", "cvars.ini");

    SanityEngine* g_engine{nullptr};

    struct AtmosphereMaterial {
        glm::vec3 sun_vector;
    };

    static void error_callback(const int error, const char* description) { logger->error("%s (GLFW error %d}", description, error); }

    static void key_func(GLFWwindow* window, const int key, int /* scancode */, const int action, const int mods) {
        auto* input_manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));

        input_manager->on_key(key, action, mods);
    }

    static void mouse_button_func(GLFWwindow* window, const int button, const int action, const int mods) {
        auto* input_manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));

        input_manager->on_mouse_button(button, action, mods);
    }

    std::filesystem::path SanityEngine::executable_directory;

    SanityEngine::SanityEngine(const std::filesystem::path& executable_directory_in)
        : input_manager{Rx::make_ptr<InputManager>(RX_SYSTEM_ALLOCATOR)}, world{global_registry} {
        logger->info("HELLO HUMAN");

        executable_directory = executable_directory_in;

        const auto cvar_ini_filepath = executable_directory / cvar_ini_file_name->get().data();
        const auto cvar_init_filepath_string = cvar_ini_filepath.string();
        if(!console_context.load(cvar_init_filepath_string.c_str())) {
            logger->warning("Could not load cvars from file %s (full path %s). Using default values",
                            cvar_ini_file_name->get().data(),
                            cvar_init_filepath_string.c_str());
        }

        {
            ZoneScoped;

            {
                ZoneScopedN("glfwInit");
                if(!glfwInit()) {
                    Rx::abort("Could not initialize GLFW");
                }
            }

            glfwSetErrorCallback(error_callback);

            {
                ZoneScopedN("glfwCreateWindow");
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
                glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
                window = glfwCreateWindow(1920, 1080, "Sanity Engine", nullptr, nullptr);
                if(window == nullptr) {
                    Rx::abort("Could not create GLFW window");
                }
            }

            logger->info("Created window");

            glfwSetWindowUserPointer(window, input_manager.get());

            // TODO: Only enable this in play-in-editor mode
            // glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            glfwSetKeyCallback(window, key_func);
            glfwSetMouseButtonCallback(window, mouse_button_func);

            register_engine_component_type_reflection();

            renderer = Rx::make_ptr<renderer::Renderer>(RX_SYSTEM_ALLOCATOR, window);
            logger->info("Initialized renderer");

            asset_registry = Rx::make_ptr<AssetRegistry>(RX_SYSTEM_ALLOCATOR, "data/Content");

            world.create_planetary_sky(*renderer);

            create_first_person_player();

            if(*show_frametime_display) {
                make_frametime_display();
            }

            if(*show_console) {
                make_console_window();
            }

            register_cvar_change_listeners();

            imgui_adapter = Rx::make_ptr<DearImguiAdapter>(RX_SYSTEM_ALLOCATOR, window, *renderer);

            frame_timer.start();

            logger->info("Constructed SanityEngine");
        }
    }

    SanityEngine::~SanityEngine() {
        const auto cvar_ini_filepath = Rx::String::format("%s/%s", executable_directory, cvar_ini_file_name->get().data());
        if(!console_context.save(cvar_ini_filepath.data())) {
            Rx::abort("Could not save cvars to file %s (full path %s)", cvar_ini_file_name->get().data(), cvar_ini_filepath);
        }

        glfwDestroyWindow(window);

        glfwTerminate();

        logger->warning("REMAIN INDOORS");
    }

    void SanityEngine::register_tick_function(Rx::Function<void(Float32)>&& tick_function) {
        tick_functions.emplace_back(Rx::Utility::move(tick_function));
    }

    void SanityEngine::register_system(const std::string& name, std::unique_ptr<System>&& system) {
        systems.emplace(name, std::move(system));
    }

    void SanityEngine::tick() {
        FrameMark;

        ZoneScoped;

        frame_timer.stop();
        const auto frame_duration = frame_timer.elapsed();
        frame_timer.start();

        const auto render_delta_time = static_cast<Float32>(frame_duration.total_seconds());

        accumulator += render_delta_time;

        const auto tick_delta_time = simulation_timestep->get();

        frame_count++;
        renderer->begin_frame(frame_count);

        while(accumulator >= tick_delta_time) {
            ZoneScopedN("Simulation tick");

            // if(player_controller) {
            //     player_controller->update_player_transform(delta_time);
            // }

            tick_functions.each_fwd([&](const Rx::Function<void(Float32)>& tick_function) { tick_function(tick_delta_time); });

            for(const auto& system : systems | std::views::values) {
                system->tick(tick_delta_time);
            }

            accumulator -= tick_delta_time;
            time_since_application_start += tick_delta_time;
        }

        // TODO: The final touch from https://gafferongames.com/post/fix_your_timestep/

#ifdef NDEBUG
        if(glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_TRUE) {
#endif
            // Only render when the window is visible
            render(render_delta_time);
#ifdef NDEBUG
        }
#endif

        renderer->end_frame();

        framerate_tracker.add_frame_time(render_delta_time);
    }

    TypeReflection& SanityEngine::get_type_reflector() { return type_reflector; }

    entt::entity SanityEngine::get_player() const { return player; }

    World& SanityEngine::get_world() { return world; }

    entt::registry& SanityEngine::get_entity_registry() { return global_registry; }

    GLFWwindow* SanityEngine::get_window() const { return window; }

    renderer::Renderer& SanityEngine::get_renderer() const { return *renderer; }

    InputManager& SanityEngine::get_input_manager() const { return *input_manager; }

    Uint32 SanityEngine::get_frame_count() const { return frame_count; }

    void SanityEngine::register_cvar_change_listeners() {
        show_frametime_display->on_change([&](Rx::Console::Variable<bool>& var) {
            if(var) {
                make_frametime_display();
            } else {
                destroy_frametime_display();
            }
        });

        show_console->on_change([&](Rx::Console::Variable<bool>& var) {
            if(var) {
                make_console_window();
            } else {
                destroy_console_window();
            }
        });
    }

    void SanityEngine::register_engine_component_type_reflection() {
        type_reflector.register_type_name<Actor>("Sanity Actor");
        type_reflector.register_type_name<TransformComponent>("Transform");

        type_reflector.register_type_name<renderer::StandardRenderableComponent>("Standard Renderable");
        type_reflector.register_type_name<renderer::PostProcessingPassComponent>("Post Processing Class");
        type_reflector.register_type_name<renderer::RaytracingObjectComponent>("Raytracing Object");
        type_reflector.register_type_name<renderer::CameraComponent>("Camera");
        type_reflector.register_type_name<renderer::LightComponent>("Light");
        type_reflector.register_type_name<renderer::SkyComponent>("Skybox");
        type_reflector.register_type_name<renderer::FluidVolumeComponent>("Fluid Volume");
    }

    void SanityEngine::make_frametime_display() {
        if(!frametime_display_entity) {
            frametime_display_entity = global_registry.create();
            global_registry.emplace<ui::UiComponent>(*frametime_display_entity,
                                                     Rx::make_ptr<ui::FramerateDisplay>(RX_SYSTEM_ALLOCATOR, framerate_tracker));
        }
    }

    void SanityEngine::destroy_frametime_display() {
        if(frametime_display_entity) {
            global_registry.destroy(*frametime_display_entity);
        }
    }

    void SanityEngine::make_console_window() {
        if(!console_window_entity) {
            console_window_entity = global_registry.create();
            auto& comp = global_registry.emplace<ui::UiComponent>(*console_window_entity,
                                                                  Rx::make_ptr<ui::ConsoleWindow>(RX_SYSTEM_ALLOCATOR, console_context));
            auto* console_window = static_cast<ui::Window*>(comp.panel.get());
            console_window->is_visible = true;
        }
    }

    void SanityEngine::destroy_console_window() {
        if(console_window_entity) {
            global_registry.destroy(*console_window_entity);
        }
    }

    void SanityEngine::create_first_person_player() {
        auto& player_actor = create_actor(global_registry, "First Person Player");
        player = player_actor.entity;

        auto& transform_component = player_actor.get_component<TransformComponent>();
        transform_component.transform.location = {0.f, 1.63f, 2.f};
        transform_component.transform.rotation = glm::angleAxis(0.0f, glm::vec3{1, 0, 0});

        player_actor.add_component<renderer::CameraComponent>();

        logger->info("Created flycam");
    }

    void SanityEngine::render(const float delta_time) {
        imgui_adapter->draw_ui(global_registry.view<ui::UiComponent>());

        renderer->render_frame(global_registry, delta_time);

    }

    void initialize_g_engine(const std::filesystem::path& executable_directory) { g_engine = new SanityEngine{executable_directory}; }
} // namespace sanity::engine
