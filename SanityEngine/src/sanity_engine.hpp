﻿#pragma once
#include <memory>

#include <entt/entity/registry.hpp>

#include "debugging/renderdoc_app.h"
#include "renderer/renderer.hpp"
#include "settings.hpp"

/*!
 * \brief Main class for my glorious engine
 */
class SanityEngine {
public:
    /*!
     * \brief Initializes the engine, including loading static data
     */
    SanityEngine();

    /*!
     * \brief De-initializes the engine, flushing all logs
     */
    ~SanityEngine();

    /*!
     * \brief Runs the main loop of the engine. This method eventually returns, after the user is finished playing their game
     */
    void run();

private:
    Settings settings;

    std::unique_ptr<RENDERDOC_API_1_3_0> renderdoc;

    std::unique_ptr<renderer::Renderer> renderer;

    GLFWwindow* window;

    entt::registry registry;

    void create_debug_cube();

    /*!
     * \brief Ticks the engine, advancing time by the specified amount
     */
    void tick(double delta_time);
};