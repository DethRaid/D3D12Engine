#pragma once

#include "atmospheric_scattering.hlsl"
#include "brdf_diffuse.hlsl"
#include "shadow.hlsl"
#include "skybox.hlsl"
#include "standard_root_signature.hlsl"

struct StandardVertex {
    float3 position : Position;
    float3 normal : Normal;
    float4 color : Color;
    // uint material_index : MATERIALINDEX;
    float2 texcoord : Texcoord;
};

#define BYTES_PER_VERTEX 9

#define STANDARD_ROUGHNESS 0.01

#define RAY_START_OFFSET_ALONG_NORMAL 0.01f

uint3 get_indices(uint triangle_index) {
    uint base_index = (triangle_index * 3);
    int address = (base_index * 4);
    return indices.Load3(address);
}

StandardVertex get_vertex(int address) {
    StandardVertex v;
    v.position = asfloat(vertices.Load3(address));
    address += (3 * 4);
    v.normal = asfloat(vertices.Load3(address));
    address += (3 * 4);
    v.color = asfloat(vertices.Load4(address));
    address += (4); // Vertex colors are only only one byte per component
    v.texcoord = asfloat(vertices.Load2(address));

    return v;
}

StandardVertex get_vertex_attributes(uint triangle_index, float2 barycentrics) {
    uint3 indices = get_indices(triangle_index);

    StandardVertex v0 = get_vertex((indices[0] * BYTES_PER_VERTEX) * 4);
    StandardVertex v1 = get_vertex((indices[1] * BYTES_PER_VERTEX) * 4);
    StandardVertex v2 = get_vertex((indices[2] * BYTES_PER_VERTEX) * 4);

    StandardVertex v;
    v.position = v0.position + barycentrics.x * (v1.position - v0.position) + barycentrics.y * (v2.position - v0.position);
    v.normal = v0.normal + barycentrics.x * (v1.normal - v0.normal) + barycentrics.y * (v2.normal - v0.normal);
    v.color = v0.color + barycentrics.x * (v1.color - v0.color) + barycentrics.y * (v2.color - v0.color);
    v.texcoord = v0.texcoord + barycentrics.x * (v1.texcoord - v0.texcoord) + barycentrics.y * (v2.texcoord - v0.texcoord);

    return v;
}

struct SanityRayHit {
    float3 position;
    float3 normal;
    float3 brdf_result;
};

/*!
 * \brief Sample the light that's coming from a given direction to a given point
 *
 * \return A float4 where the rgb are the incoming light and the a is 1 if we hit a surface, 0 is we're sampling the sky
 */
float4 get_incoming_light(in float3 ray_origin,
                          in float3 direction,
                          in Light sun,
                          inout RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query,
                          in float2 noise_texcoord,
                          in Texture2D noise,
                          out StandardVertex vertex,
                          out MaterialData material) {

    RayDesc ray;
    ray.Origin = ray_origin;
    ray.TMin = 0.001;
    ray.Direction = direction;
    ray.TMax = 1000; // TODO: Pass this in with a CB

    // Set up work
    query.TraceRayInline(raytracing_scene, 0, 0xFF, ray);

    // Actually perform the trace
    query.Proceed();

    if(query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        uint triangle_index = query.CommittedPrimitiveIndex();
        float2 barycentrics = query.CommittedTriangleBarycentrics();
        vertex = get_vertex_attributes(triangle_index, barycentrics);

        uint material_id = query.CommittedInstanceContributionToHitGroupIndex();
        material = material_buffer[material_id];

        Texture2D albedo_tex = textures[material.albedo_idx];
        float3 hit_albedo = pow(albedo_tex.Sample(bilinear_sampler, vertex.texcoord).rgb, 1.0 / 2.2);

        Texture2D metalness_rougness_tex = textures[material.metallic_roughness_idx];
        float2 hit_f0_roughness = metalness_rougness_tex.Sample(bilinear_sampler, vertex.texcoord).gb;

        float t = query.CommittedRayT();
        if(t <= 0) {
            // Ray is stuck
            return 0;
        }

        float shadow = saturate(raytrace_shadow(sun, direction * t + ray_origin, vertex.normal, noise_texcoord, noise));

        // Calculate the diffuse light reflected by the hit point along the ray
        float3 lit_hit_surface = brdf(hit_albedo, hit_f0_roughness.x, vertex.normal, hit_f0_roughness.y, -sun.direction, ray.Direction) *
                                 sun.color * shadow;

        return float4(lit_hit_surface, 1.0);

    } else {
        // Sample the atmosphere

        Texture2D skybox = textures[per_frame_data[0].sky_texture_idx];
        const float2 skybox_uv = equi_uvs(direction);

        float3 skybox_sample = skybox.Sample(bilinear_sampler, skybox_uv).rgb;

        return float4(skybox_sample, 0);
    }
}

// from http://www.rorydriscoll.com/2009/01/07/better-sampling/
float3 CosineSampleHemisphere(float2 uv) {
    float r = sqrt(uv.x);
    float theta = 2 * PI * uv.y;
    float x = r * cos(theta);
    float z = r * sin(theta);
    return float3(x, sqrt(max(0, 1 - uv.x)), z);
}

// Adapted from https://github.com/NVIDIA/Q2RTX/blob/9d987e755063f76ea86e426043313c2ba564c3b7/src/refresh/vkpt/shader/utils.glsl#L240
float3x3 construct_ONB_frisvad(float3 normal) {
    float3x3 ret;
    ret[1] = normal;
    if(normal.z < -0.999805696f) {
        ret[0] = float3(0.0f, -1.0f, 0.0f);
        ret[2] = float3(-1.0f, 0.0f, 0.0f);
    } else {
        float a = 1.0f / (1.0f + normal.z);
        float b = -normal.x * normal.y * a;
        ret[0] = float3(1.0f - normal.x * normal.x * a, b, -normal.x);
        ret[2] = float3(b, 1.0f - normal.y * normal.y * a, -normal.y);
    }
    return ret;
}

float2 wang_hash(uint2 seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return float2(seed) / (int((uint(1) << 31)) - 1);
}

float3 raytrace_reflections(float3 position_worldspace,
                            float3 normal,
                            const float3 eye_vector,
                            float metalness,
                            float roughness,
                            const in float2 noise_texcoord,
                            const Light sun,
                            Texture2D noise) {
    const uint num_specular_rays = 2;

    const uint num_bounces = 2;

    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;

    float3 ray_origin = position_worldspace;
    float3 surface_normal = normal;
    float3 view_vector = eye_vector;

    float3 reflection = 0;

    for(uint ray_idx = 1; ray_idx <= num_specular_rays; ray_idx++) {
        float3 specular_reflection_factor = 1;
        float3 light_sample = 0;

        for(uint bounce_idx = 1; bounce_idx <= num_bounces; bounce_idx++) {
            const float2 nums = noise.Sample(bilinear_sampler, noise_texcoord * ray_idx * bounce_idx).rg;
            float3 ray_direction = CosineSampleHemisphere(nums);
            const float pdf = ray_direction.y;
            const float3x3 onb = transpose(construct_ONB_frisvad(surface_normal));
            ray_direction = normalize(mul(onb, ray_direction));
            ray_direction = lerp(surface_normal, ray_direction, roughness);

            if(dot(surface_normal, ray_direction) < 0) {
                ray_direction *= -1;
            }

            float3 noise_float3 = noise.Sample(bilinear_sampler, noise_texcoord * ray_idx * bounce_idx).rgb;
            noise_float3 = normalize(noise_float3) * roughness;
            const float3 reflection_normal = normalize(surface_normal + noise_float3);
            ray_direction = reflect(view_vector, reflection_normal);

            specular_reflection_factor *= Fr_CookTorance(normal, roughness, metalness, ray_direction, view_vector);

            StandardVertex hit_vertex;
            MaterialData hit_material;

            float4 incoming_light = get_incoming_light(ray_origin,
                                                       ray_direction,
                                                       sun,
                                                       query,
                                                       noise_texcoord * ray_idx * bounce_idx,
                                                       noise,
                                                       hit_vertex,
                                                       hit_material) /
                                    (2.0f * PI);

            light_sample += specular_reflection_factor * incoming_light.rgb;

            if(incoming_light.a > 0.05) {
                // set up next ray
                ray_origin = hit_vertex.position;
                surface_normal = hit_vertex.normal;
                Texture2D metallic_roughness = textures[hit_material.metallic_roughness_idx];
                float4 tex_sample = metallic_roughness.Sample(bilinear_sampler, hit_vertex.texcoord);
                metalness = tex_sample.g > 0.5 ? 0.8 : 0.02;
                roughness = tex_sample.b;
                view_vector = ray_direction;

            } else {
                // Ray escaped into the sky
                break;
            }
        }

        reflection += light_sample / (float) num_bounces;
    }

    return reflection / (float) num_specular_rays;
}

/*!
 * \brief Calculate the raytraced indirect light that hits a surface
 */
float3 raytrace_global_illumination(const in float3 position_worldspace,
                                    const in float3 normal,
                                    const in float3 eye_vector,
                                    const in float3 albedo,
                                    const in float f0,
                                    const in float roughness,
                                    const in float2 noise_texcoord,
                                    const in Light sun,
                                    const in Texture2D noise) {
    const uint num_indirect_rays = 2;

    const uint num_bounces = 2;

    // TODO: In theory, we should walk the ray to collect all transparent hits that happen closer than the closest opaque hit, and filter
    // the opaque hit's light through the transparent surfaces. This will be implemented l a t e r when I feel more comfortable with ray
    // shaders

    float3 indirect_light = 0;

    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;

    float3 ray_origin = position_worldspace;
    float3 surface_normal = normal;
    float3 surface_albedo = albedo;
    float3 view_vector = eye_vector;

    for(uint ray_idx = 1; ray_idx <= num_indirect_rays; ray_idx++) {
        float3 diffuse_reflection_factor = 1;
        float3 light_sample = 0;

        for(uint bounce_idx = 1; bounce_idx <= num_bounces; bounce_idx++) {
            const float2 nums = noise.Sample(bilinear_sampler, noise_texcoord * ray_idx * bounce_idx).rg;
            float3 ray_direction = CosineSampleHemisphere(nums);
            const float pdf = ray_direction.y;
            const float3x3 onb = transpose(construct_ONB_frisvad(surface_normal));
            ray_direction = normalize(mul(onb, ray_direction));

            if(dot(surface_normal, ray_direction) < 0) {
                ray_direction *= -1;
            }

            diffuse_reflection_factor *= Fd_Lambert(surface_normal, ray_direction) * surface_albedo / pdf;

            StandardVertex hit_vertex;
            MaterialData hit_material;

            float4 incoming_light = get_incoming_light(ray_origin,
                                                       ray_direction,
                                                       sun,
                                                       query,
                                                       noise_texcoord * ray_idx * bounce_idx,
                                                       noise,
                                                       hit_vertex,
                                                       hit_material) /
                                    (2.0f * PI);
            light_sample += diffuse_reflection_factor * incoming_light.rgb;

            if(incoming_light.a > 0.05) {
                // set up next ray
                ray_origin = hit_vertex.position;
                surface_normal = hit_vertex.normal;
                Texture2D albedo_tex = textures[hit_material.albedo_idx];
                surface_albedo = albedo_tex.Sample(bilinear_sampler, hit_vertex.texcoord).rgb;
                view_vector = ray_direction;

            } else {
                // Ray escaped into the sky
                break;
            }
        }

        indirect_light += light_sample / (float) num_bounces;
    }

    float3 indirect_diffuse = indirect_light / (float) num_indirect_rays;

    return indirect_diffuse;
}

float3 get_total_reflected_light(
    Camera camera, VertexOutput input, float3 base_color, float3 normal, float metalness, float roughness, Texture2D noise) {
    const Light sun = lights[0]; // The sun is ALWAYS at index 0

	float3 light_vector_worldspace = normalize(-sun.direction);
    light_vector_worldspace.y *= -1;    // uhhhhhhhhhhhhhhhhhhh

    // Transform worldspace position into viewspace position
    const float4 position_viewspace = mul(camera.view, float4(input.position_worldspace, 1));

    // Treat viewspace position as the view vector
    float3 view_vector_viewspace = normalize(position_viewspace.xyz);

    // Transform view vector into worldspace
    const float3 view_vector_worldspace = normalize(mul(camera.inverse_view, float4(view_vector_viewspace, 0)).xyz);

    const float3 light_from_sun = brdf(base_color.rgb,
                                       input.normal_worldspace,
                                       metalness,
                                       roughness,
                                       light_vector_worldspace,
                                       view_vector_worldspace);

    float sun_shadow = 0;

    float2 noise_tex_size = float2(1024.f, 1024.f);
    float2 noise_texcoord = input.location_ndc.xy / noise_tex_size;
    const float2 offset = noise.Sample(bilinear_sampler, noise_texcoord * per_frame_data[0].time_since_start).rg;
    noise_texcoord *= offset;

    // Only cast shadow rays if the pixel faces the light source
    if(length(light_from_sun) > 0) {
        sun_shadow = raytrace_shadow(sun, input.position_worldspace, input.normal_worldspace, noise_texcoord, noise);
    }

    const float3 direct_light = light_from_sun * sun_shadow;
    return direct_light;

    const float3 indirect_light = raytrace_global_illumination(input.position_worldspace,
                                                               input.normal_worldspace,
                                                               view_vector_worldspace,
                                                               base_color,
                                                               metalness,
                                                               roughness,
                                                               noise_texcoord,
                                                               sun,
                                                               noise);
    return indirect_light;

    const float3 reflection = raytrace_reflections(input.position_worldspace,
                                                   input.normal_worldspace,
                                                   view_vector_worldspace,
                                                   metalness,
                                                   roughness,
                                                   noise_texcoord,
                                                   sun,
                                                   noise);
    return reflection;

    return direct_light + indirect_light + reflection;
}
