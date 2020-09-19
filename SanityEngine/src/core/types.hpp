#pragma once

#include <wrl/client.h>

#include "rx/core/types.h"
#include "rx/math/mat4x4.h"
#include "rx/math/vec2.h"
#include "rx/math/vec3.h"
#include "rx/math/vec4.h"

using Microsoft::WRL::ComPtr;

using Rx::Byte;
using Rx::Size;

using Rx::Uint8;
using Rx::Uint16;
using Rx::Uint32;
using Rx::Uint64;

using Rx::Sint32;
using Rx::Sint64;
using Int32 = Sint32;
using Int64 = Sint64;

using Rx::Float32;
using Rx::Float64;

using Rx::Math::Vec2f;
using Rx::Math::Vec2i;
using Vec2u = Rx::Math::Vec2<Uint32>;
using Vec2d = Rx::Math::Vec2<Float64>;

using Rx::Math::Vec3f;
using Rx::Math::Vec3i;
using Vec3u = Rx::Math::Vec3<Uint32>;
using Vec3d = Rx::Math::Vec3<Float64>;

using Rx::Math::Vec4f;
using Rx::Math::Vec4i;
using Vec4u = Rx::Math::Vec4<Uint32>;
using Vec4d = Rx::Math::Vec4<Float64>;

using Rx::Math::Mat4x4f;

using Rx::operator""_z;
using Rx::operator""_u8;
using Rx::operator""_u16;
using Rx::operator""_u32;
using Rx::operator""_u64;
