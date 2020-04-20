#include "resources.hpp"

namespace rhi {
    size_t size_in_bytes(const ImageFormat format) {
        switch(format) {
            case ImageFormat::Rgba32F:
                return 128;

            case ImageFormat::Rgba8:
                [[fallthrough]];
            case ImageFormat::Depth32:
                [[fallthrough]];
            case ImageFormat::Depth24Stencil8:
                [[fallthrough]];
            default:
                return 32;
        }
    }
} // namespace render
