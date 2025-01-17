#pragma once

#include "rhi/resources.hpp"

namespace sanity::engine::renderer {
    /*!
     * \brief Representation of a generic material
     */
    class Material {
    public:
        template<typename MaterialStruct>
        MaterialStruct& as();

    private:
        /*!
         * \brief Pointer to the 
         */
        void* ptr = nullptr;
    };
}
