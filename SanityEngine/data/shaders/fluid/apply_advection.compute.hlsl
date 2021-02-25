#include "inc/standard_root_signature.hlsl"
#include "fluid_sim.hpp"

float4 _Size;
float _DeltaTime, _Dissipate, _Decay, _Forward;

StructuredBuffer<float3> _Velocity;
StructuredBuffer<float> _Obstacles;

RWStructuredBuffer<float> _Write1f;
StructuredBuffer<float> _Read1f;

RWStructuredBuffer<float3> _Write3f;
StructuredBuffer<float3> _Read3f;

StructuredBuffer<float> _Phi_n_1_hat, _Phi_n_hat;

[numthreads(FLUID_SIM_NUM_THREADS, FLUID_SIM_NUM_THREADS, FLUID_SIM_NUM_THREADS)]
void main(uint3 id : SV_DispatchThreadID) {
	const FrameConstants frame_constants = get_frame_constants();
    GET_CURRENT_DATA(GpuFluidVolumeState, fluid_volume);	

    // if(_Obstacles[idx] > 0.1) {
    //     _Write1f[idx] = 0;
    //     return;
    // }

	Texture3D density_in = textures3d[fluid_volume.density_textures[0]];
	Texture3D temperature_in = textures3d[fluid_volume.temperature_textures[0]];
	Texture3D reaction_in = textures3d[fluid_volume.reaction_textures[0]];
	Texture3D velocity_in = textures3d[fluid_volume.velocity_textures[0]];

	RWTexture3D<float> density_out = uav_textures3d_r[fluid_volume.density_textures[1]];
	RWTexture3D<float> temperature_out = uav_textures3d_r[fluid_volume.temperature_textures[1]];
	RWTexture3D<float> reaction_out = uav_textures3d_r[fluid_volume.reaction_textures[1]];
	RWTexture3D<float4> velocity_out = uav_textures3d_rgba[fluid_volume.velocity_textures[1]];

	float3 volume_voxel_size;
	velocity_in.GetDimensions(volume_voxel_size.x, volume_voxel_size.y, volume_voxel_size.z);
	const float3 my_uv = float3(id) / volume_voxel_size;

	const float3 advection_offset = frame_constants.delta_time * velocity_in.Sample(bilinear_sampler, my_uv).xyz;
    const float3 advection_uv = my_uv + advection_offset;

	float4 data;
	data.x = density_in.Sample(bilinear_sampler, advection_uv).r;
	data.y = temperature_in.Sample(bilinear_sampler, advection_uv).r;
	data.z = reaction_in.Sample(bilinear_sampler, advection_uv).r;
	data.w = velocity_in.Sample(bilinear_sampler, advection_uv).r;

	data = data * fluid_volume.dissipation - fluid_volume.decay;
	data.xyz = max(0, data.xyz);

	density_out[my_uv] = data.x;
	temperature_out[my_uv] = data.y;
	reaction_out[my_uv] = data.z;
	velocity_out[my_uv] = data.w;
}
