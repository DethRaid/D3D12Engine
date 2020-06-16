#pragma once

#include <rx/core/array.h>
#include <rx/core/map.h>
#include <rx/core/types.h>
#include <rx/core/vector.h>

#include "core/types.hpp"
#include "rx/core/optional.h"

// Dual contouring adapted from https://github.com/BorisTheBrave/mc-dc/blob/master/dual_contour_3d.py

struct Quad {
    Uint32 v1;
    Uint32 v2;
    Uint32 v3;
    Uint32 v4;

    Quad swap(bool swap);
};

/*!
 * \brief A mesh generated by the dual contouring method
 *
 * This mesh should be triangulated before being sent to the GPU
 */
struct DualContouringMesh {
    Rx::Vector<Vec3f> vertex_positions;

    Rx::Vector<Vec3f> normals;

    Rx::Vector<Quad> faces;
};

/*!
 * \brief Computes the dual-contoured mesh that best fits the provided distance field
 *
 * The distance field should have negative numbers outside the surface and positive numbers inside the surface
 */
template <Uint32 Width, Uint32 Height, Uint32 Depth>
[[nodiscard]] DualContouringMesh dual_contour(const Rx::Array<Int32[Width * Height * Depth]>& distance_field);

// ReSharper disable once CppInconsistentNaming
namespace _detail {
    template <Uint32 Width, Uint32 Height, Uint32 Depth>
    [[nodiscard]] DualContouringMesh dual_contour(const Rx::Array<Int32[Width * Height * Depth]>& distance_field);

    template <Uint32 Width, Uint32 Height, Uint32 Depth>
    [[nodiscard]] Rx::Optional<Vec3f> dual_contour_find_best_vertex(const Rx::Array<Int32[Width * Height * Depth]>& distance_field,
                                                                    Uint32 x,
                                                                    Uint32 y,
                                                                    Uint32 z);

    template <Uint32 Width, Uint32 Height, Uint32 Depth>
    [[nodiscard]] Vec3f normal_at_location(const Rx::Array<Int32[Width * Height * Depth]>& distance_field, const Vec3f& location);

    [[nodiscard]] Vec3f solve_qef(Uint32 x, Uint32 y, Uint32 z, const Rx::Vector<Vec3f>& vertices, const Rx::Vector<Vec3f>& normals);

    float adapt(const Int32& v0, const Int32& v1);

    template <Uint32 Width, Uint32 Height>
    [[nodiscard]] Size idx_from_xyz(Uint32 x, Uint32 y, Uint32 z);

    template <Uint32 Width, Uint32 Height>
    [[nodiscard]] Size idx_from_xyz(const Vec3u& xyz);

    template <Uint32 Width, Uint32 Height>
    [[nodiscard]] Size idx_from_xyz(const Vec3f& xyz);

    template <Uint32 Width, Uint32 Height, Uint32 Depth>
    [[nodiscard]] DualContouringMesh dual_contour(const Rx::Array<Int32[Width * Height * Depth]>& distance_field) {
        Rx::Vector<Vec3f> vertices;
        Rx::Map<Vec3u, Uint32> indices;

        for(Uint32 z = 0; z < Depth; z++) {
            for(Uint32 y = 0; y < Height; y++) {
                for(Uint32 x = 0; x < Width; x++) {
                    const auto vert = dual_contour_find_best_vertex<Width, Height, Depth>(distance_field, x, y, z);
                    if(!vert) {
                        continue;
                    }

                    vertices.push_back(*vert);
                    indices.insert(Vec3u{x, y, z}, static_cast<Uint32>(vertices.size() - 1));
                }
            }
        }

        auto faces = Rx::Vector<Quad>{};
        faces.reserve(distance_field.size());

        for(Uint32 z = 0; z < Depth; z++) {
            for(Uint32 y = 0; y < Height; y++) {
                for(Uint32 x = 0; x < Width; x++) {
                    if(x > 0 && y > 0) {
                        const auto solid1 = distance_field[idx_from_xyz<Width, Height>(x, y, z)] > 0;
                        const auto solid2 = distance_field[idx_from_xyz<Width, Height>(x, y, z + 1)] > 0;
                        if(solid1 != solid2) {
                            faces.push_back(Quad{*indices.find(Vec3u(x - 1, y - 1, z)),
                                                 *indices.find(Vec3u(x - 0, y - 1, z)),
                                                 *indices.find(Vec3u(x - 1, y - 0, z)),
                                                 *indices.find(Vec3u(x - 0, y - 0, z))}
                                                .swap(solid2));
                        }
                    }

                    if(x > 0 && z > 0) {
                        const auto solid1 = distance_field[idx_from_xyz<Width, Height>(x, y, z)] > 0;
                        const auto solid2 = distance_field[idx_from_xyz<Width, Height>(x, y + 1, z)] > 0;
                        if(solid1 != solid2) {
                            faces.push_back(Quad{*indices.find(Vec3u(x - 1, y, z - 1)),
                                                 *indices.find(Vec3u(x - 0, y, z - 1)),
                                                 *indices.find(Vec3u(x - 0, y, z + 0)),
                                                 *indices.find(Vec3u(x - 1, y, z + 0))}
                                                .swap(solid1));
                        }
                    }

                    if(y > 0 && z > 0) {
                        const auto solid1 = distance_field[idx_from_xyz<Width, Height>(x, y, z)] > 0;
                        const auto solid2 = distance_field[idx_from_xyz<Width, Height>(x + 1, y, z)] > 0;
                        if(solid1 != solid2) {
                            faces.push_back(Quad{*indices.find(Vec3u(x, y - 1, z - 1)),
                                                 *indices.find(Vec3u(x, y - 0, z - 1)),
                                                 *indices.find(Vec3u(x, y - 0, z - 0)),
                                                 *indices.find(Vec3u(x, y - 1, z - 0))}
                                                .swap(solid2));
                        }
                    }
                }
            }
        }

        Rx::Vector<Vec3f> normals;
        normals.reserve(vertices.size());

        vertices.each_fwd([&](const Vec3f& vertex_location) {
            normals.push_back(normal_at_location<Width, Height, Depth>(distance_field, vertex_location));
        });

        return DualContouringMesh{Rx::Utility::move(vertices), Rx::Utility::move(normals), Rx::Utility::move(faces)};
    }

    template <Uint32 Width, Uint32 Height, Uint32 Depth>
    Rx::Optional<Vec3f> dual_contour_find_best_vertex(const Rx::Array<Int32[Width * Height * Depth]>& distance_field,
                                                      const Uint32 x,
                                                      const Uint32 y,
                                                      const Uint32 z) {
        // Sample the distance field at the corners of this space
        auto v = Rx::Array<Rx::Array<Rx::Array<Int32[2]>[2]>[2]> {};
        for(Uint32 dz = 0; dz < 2; dz++) {
            for(Uint32 dy = 0; dy < 2; dy++) {
                for(Uint32 dx = 0; dx < 2; dx++) {
                    v[dz][dy][dx] = distance_field[idx_from_xyz<Width, Height>(x + dx, y + dy, z + dz)];
                }
            }
        }

        // For each edge, identify where there is a sign change
        // There are four edges along each of the three axis
        auto changes = Rx::Vector<Vec3f>{};
        changes.reserve(24);
        for(Uint32 dy = 0; dy < 2; dy++) {
            for(Uint32 dx = 0; dx < 2; dx++) {
                if((v[0][dy][dx] > 0) != (v[1][dy][dx] > 0)) {
                    changes.emplace_back(static_cast<Float32>(x + dx),
                                         static_cast<Float32>(y + dy),
                                         static_cast<Float32>(z + adapt(v[0][dy][dx], v[1][dy][dx])));
                }
            }
        }

        for(Uint32 dz = 0; dz < 2; dz++) {
            for(Uint32 dx = 0; dx < 2; dx++) {
                if((v[dz][0][dx] > 0) != (v[dz][1][dx] > 0)) {
                    changes.emplace_back(static_cast<Float32>(x + dx),
                                         static_cast<Float32>(y + adapt(v[dz][0][dx], v[dz][1][dx])),
                                         static_cast<Float32>(z + dz));
                }
            }
        }

        for(Uint32 dz = 0; dz < 2; dz++) {
            for(Uint32 dy = 0; dy < 2; dy++) {
                if((v[dz][dy][0] > 0) != (v[dz][dy][1] > 0)) {
                    changes.emplace_back(static_cast<Float32>(x + adapt(v[dz][dy][0], v[dz][dy][1])),
                                         static_cast<Float32>(y + dy),
                                         static_cast<Float32>(z + dz));
                }
            }
        }

        if(changes.is_empty()) {
            return Rx::nullopt;
        }

        // For each sign change location v[i], we find the normal n[i].
        // The error term we are trying to minimize is sum(dot(x - v[i], n[i]) ^ 2)

        // In other words, minimize || A* x - b || ^2 where A and b are a matrix and vector
        // derived from v and n

        auto normals = Rx::Vector<Vec3f>{};
        normals.reserve(changes.size());
        changes.each_fwd(
            [&](const Vec3f& location) { normals.push_back(normal_at_location<Width, Height, Depth>(distance_field, location)); });

        return solve_qef(x, y, z, changes, normals);
    }

    template <Uint32 Width, Uint32 Height, Uint32 Depth>
    Vec3f normal_at_location(const Rx::Array<Int32[Width * Height * Depth]>& distance_field, const Vec3f& location) {
        auto plus_x_idx = idx_from_xyz<Width, Height>(location + Vec3f{1, 0, 0});
        auto minus_x_idx = idx_from_xyz<Width, Height>(location + Vec3f{-1, 0, 0});
        auto plus_y_idx = idx_from_xyz<Width, Height>(location + Vec3f{0, 1, 0});
        auto minus_y_idx = idx_from_xyz<Width, Height>(location + Vec3f{0, -1, 0});
        auto plus_z_idx = idx_from_xyz<Width, Height>(location + Vec3f{0, 0, 1});
        auto minus_z_idx = idx_from_xyz<Width, Height>(location + Vec3f{0, 0, -1});

        return Vec3f{(distance_field[plus_x_idx] - distance_field[minus_x_idx]) / 2.0f,
                     (distance_field[plus_y_idx] - distance_field[minus_y_idx]) / 2.0f,
                     (distance_field[plus_z_idx] - distance_field[minus_z_idx]) / 2.0f};
    }

    template <Uint32 Width, Uint32 Height>
    Size idx_from_xyz(const Uint32 x, const Uint32 y, const Uint32 z) {
        return x + y * Width + z + Width * Height;
    }

    template <Uint32 Width, Uint32 Height>
    Size idx_from_xyz(const Vec3u& xyz) {
        return idx_from_xyz<Width, Height>(xyz.x, xyz.y, xyz.z);
    }

    template <Uint32 Width, Uint32 Height>
    Size idx_from_xyz(const Vec3f& xyz) {
        return idx_from_xyz<Width, Height>(static_cast<Uint32>(xyz.x), static_cast<Uint32>(xyz.y), static_cast<Uint32>(xyz.z));
    }
} // namespace _detail

template <Uint32 Width, Uint32 Height, Uint32 Depth>
[[nodiscard]] DualContouringMesh dual_contour(const Rx::Array<Int32[Width * Height * Depth]>& distance_field) {
    return _detail::dual_contour<Width, Height, Depth>(distance_field);
}