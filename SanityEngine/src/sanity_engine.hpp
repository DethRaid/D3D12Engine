﻿#pragma once

#include "adapters/rex/rex_wrapper.hpp"
#include "core/Prelude.hpp"
#include "core/asset_registry.hpp"
#include "entt/entity/registry.hpp"
#include "input/input_manager.hpp"
#include "player/first_person_controller.hpp"
#include "renderer/renderer.hpp"
#include "rx/core/ptr.h"
#include "rx/core/time/stop_watch.h"
#include "settings.hpp"
#include "stats/framerate_tracker.hpp"
#include "ui/dear_imgui_adapter.hpp"
#include "world/world.hpp"

/*!
 * \brief Main class for my glorious engine
 */
class [[sanity::runtime_class]] SanityEngine {
public:
    static const char* executable_directory;

    /*!
     * \brief Initializes the engine, including loading static data
     */
    explicit SanityEngine(const char* executable_directory_in);

    /*!
     * \brief De-initializes the engine
     */
    ~SanityEngine();

    /*!
     * \brief Executes a single frame, updating game logic and rendering the scene
     */
    void tick();

    [[nodiscard]] entt::entity get_player() const;

    [[nodiscard]] SynchronizedResource<entt::registry>& get_registry();

    [[nodiscard]] World* get_world() const;

	[[nodiscard]] GLFWwindow* get_window() const;

private:
    rex::Wrapper rex;

    Rx::Ptr<InputManager> input_manager;

    Rx::Ptr<renderer::Renderer> renderer;

    Rx::Ptr<DearImguiAdapter> imgui_adapter;

    FramerateTracker framerate_tracker{1000};

    // Rx::Ptr<BveWrapper> bve;

    GLFWwindow* window;

    Rx::Ptr<World> world;

    SynchronizedResource<entt::registry> registry;

    /*!
     * \brief Entity which represents the player
     *
     * SanityEngine is a singleplayer engine, end of story. Makes my life easier and increases my sanity :)
     */
    entt::entity player;

    Rx::Ptr<FirstPersonController> player_controller;

    Rx::Ptr<AssetRegistry> asset_registry;

    Rx::Time::StopWatch frame_timer;

    /*!
     * \brief Number of seconds since the engine started running
     */
    Float32 time_since_application_start = 0;

    Float32 accumulator = 0;
	
    Uint64 frame_count = 0;

    #pragma region Spawning
    void create_planetary_atmosphere();

    void make_frametime_display();

    void create_first_person_player();

    void create_environment_object_editor();

    void load_3d_object(const Rx::String& filename);
#pragma endregion

#pragma region Update loop
    void render();
#pragma endregion
};

extern SanityEngine* g_engine;

void initialize_g_engine(const char* executable_directory);
