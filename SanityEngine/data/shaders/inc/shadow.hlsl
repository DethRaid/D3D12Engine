#pragma once

#include "standard_root_signature.hlsl"

#define NUM_SHADOW_RAYS 16

#define SHADOW_RAY_BIAS 0.01

// from https://gist.github.com/keijiro/ee439d5e7388f3aafc5296005c8c3f33
// Rotation with angle (in radians) and axis
float3x3 AngleAxis3x3(float angle, float3 axis) {
    float c, s;
    sincos(angle, s, c);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return float3x3(t * x * x + c,
                    t * x * y - s * z,
                    t * x * z + s * y,
                    t * x * y + s * z,
                    t * y * y + c,
                    t * y * z - s * x,
                    t * x * z - s * y,
                    t * y * z + s * x,
                    t * z * z + c);
}

float raytrace_shadow(float3 light_vector,
                      const float angular_size,
                      const float3 position_worldspace,
                      const float3 mesh_normal_worldspace,
                      const float2 noise_texcoord) {
    light_vector.z *= -1;

    const float noise_scale = tan(angular_size);

    // Shadow ray query
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;

    float shadow_strength = 1.0;

    for(uint i = 0; i <= NUM_SHADOW_RAYS; i++) {
        // Random cone with the same angular size as the sun
        const float3 random_vector = get_random_vector_aligned_to_normal(mesh_normal_worldspace, noise_texcoord, i, NUM_SHADOW_RAYS);

        float3 ray_direction;
        if(abs(angular_size) <= PI / 2) {
            ray_direction = normalize(light_vector + random_vector * noise_scale);

        } else {
            ray_direction = normalize(light_vector / noise_scale + random_vector);
        }

        const float cos_theta = dot(ray_direction, mesh_normal_worldspace);

        RayDesc ray;
        ray.Origin = position_worldspace;
        ray.TMin = SHADOW_RAY_BIAS * (1.0 - cos_theta); // Slight offset so we don't self-intersect
        ray.Direction = ray_direction;
        ray.TMax = 1000; // TODO: Pass this in with a CB

        // Set up work
        q.TraceRayInline(raytracing_scene, 0, 0xFF, ray);

        // Actually perform the trace
        q.Proceed();

        if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            shadow_strength -= 1.0f / NUM_SHADOW_RAYS;
        }
    }

    return shadow_strength;
}