#pragma once

#include "core/types.hpp"
#include "glm/fwd.hpp"
#include "renderer/lights.hpp"
#include "renderer/mesh.hpp"
#include "renderer/hlsl/standard_material.hpp"
#include "rx/core/map.h"

namespace sanity {
    namespace engine {
        struct Transform;
    }
} // namespace sanity

namespace Rx {
    struct String;
}

namespace sanity::engine::ui {
    void draw_property(const Rx::String& label, bool& b);

    void draw_property(const Rx::String& label, Float32& f);

    void draw_property(const Rx::String& label, Float64& f);

    void draw_property(const Rx::String& label, Uint32& u);

    void draw_property(const Rx::String& label, glm::vec3& vec);

	void draw_property(const Rx::String& label, glm::uvec3& vec);

    void draw_property(const Rx::String& label, glm::quat& quat);

    void draw_property(const Rx::String& label, Transform& transform);

    void draw_property(const Rx::String& label, Rx::String& string);

    void draw_property(const Rx::String& label, renderer::Mesh& mesh);

    void draw_property(const Rx::String& label, renderer::StandardMaterialHandle& handle);

    void draw_property(const Rx::String& label, renderer::LightType& type);

    void draw_property(const Rx::String& label, renderer::GpuLight& light);

	void draw_drop_down_selector(const Rx::String& label, const Rx::Vector<Rx::String>& list_items, Uint32& selected_item);

    template <typename KeyType, typename ValueType>
    void draw_property(const Rx::String& label, Rx::Map<KeyType, ValueType>& map);

    template <typename KeyType, typename ValueType>
    void draw_property(const Rx::String& label, Rx::Map<KeyType, ValueType>& map) {
        // TODO
    }
} // namespace sanity::engine::ui
