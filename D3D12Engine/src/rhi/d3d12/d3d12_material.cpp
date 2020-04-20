#include "d3d12_material.hpp"

#include <spdlog/spdlog.h>

#include "../../core/ensure.hpp"
#include "d3d12_render_device.hpp"
#include "d3dx12.hpp"

using std::move;

namespace rhi {
    D3D12BindGroup::D3D12BindGroup(std::unordered_map<UINT, D3D12_GPU_DESCRIPTOR_HANDLE> descriptor_table_handles_in,
                                   std::vector<BoundResource<D3D12Image>> used_images_in,
                                   std::vector<BoundResource<D3D12Buffer>> used_buffers_in)
        : descriptor_table_handles{move(descriptor_table_handles_in)},
          used_images{move(used_images_in)},
          used_buffers{move(used_buffers_in)} {}

    D3D12BindGroupBuilder::D3D12BindGroupBuilder(std::unordered_map<std::string, D3D12Descriptor> descriptors_in,
                                                 std::unordered_map<UINT, D3D12_GPU_DESCRIPTOR_HANDLE> descriptor_table_handles_in,
                                                 D3D12RenderDevice& render_device_in)
        : descriptors{move(descriptors_in)},
          descriptor_table_handles{move(descriptor_table_handles_in)},
          render_device{&render_device_in} {}

    D3D12BindGroupBuilder::D3D12BindGroupBuilder(D3D12BindGroupBuilder&& old) noexcept
        : descriptors{move(old.descriptors)},
          descriptor_table_handles{move(old.descriptor_table_handles)},
          render_device{old.render_device},
          bound_buffers{move(old.bound_buffers)},
          bound_images{move(old.bound_images)} {
        old.~D3D12BindGroupBuilder();
    }

    D3D12BindGroupBuilder& D3D12BindGroupBuilder::operator=(D3D12BindGroupBuilder&& old) noexcept {
        descriptors = move(old.descriptors);
        descriptor_table_handles = move(old.descriptor_table_handles);
        bound_buffers = move(old.bound_buffers);
        bound_images = move(old.bound_images);
        render_device = old.render_device;

        old.~D3D12BindGroupBuilder();

        return *this;
    }

    BindGroupBuilder& D3D12BindGroupBuilder::set_buffer(const std::string& name, const Buffer& buffer) {
        ENSURE(descriptors.find(name) != descriptors.end(), "Could not bind buffer to variable {}: that variable does not exist!", name);

        const auto& d3d12_buffer = static_cast<const D3D12Buffer&>(buffer);
        if(const auto& buffer_slot = bound_buffers.find(name); buffer_slot != bound_buffers.end()) {
            buffer_slot->second = &d3d12_buffer;

        } else {
            bound_buffers.emplace(name, &d3d12_buffer);
        }

        return *this;
    }

    BindGroupBuilder& D3D12BindGroupBuilder::set_image(const std::string& name, const Image& image) {
        ENSURE(descriptors.find(name) != descriptors.end(), "Could not bind image to variable {}: that variable does not exist!", name);

        return set_image_array(name, {&image});
    }

    BindGroupBuilder& D3D12BindGroupBuilder::set_image_array(const std::string& name, const std::vector<const Image*>& images) {
        ENSURE(descriptors.find(name) != descriptors.end(),
               "Could not bind image array to variable {}: that variable does not exist!",
               name);
        ENSURE(!images.empty(), "Can not bind an empty image array to variable {}", name);

        std::vector<const D3D12Image*> d3d12_images;
        d3d12_images.reserve(images.size());

        for(const auto* image : images) {
            d3d12_images.push_back(static_cast<const D3D12Image*>(image));
        }

        if(auto image_array_slot = bound_images.find(name); image_array_slot != bound_images.end()) {
            image_array_slot->second = move(d3d12_images);

        } else {
            bound_images.emplace(name, move(d3d12_images));
        }

        return *this;
    }

    std::unique_ptr<BindGroup> D3D12BindGroupBuilder::build() {
        auto [images, buffers] = bind_resources_to_descriptors();

        return std::make_unique<D3D12BindGroup>(descriptor_table_handles, move(images), move(buffers));
    }

    BoundResources D3D12BindGroupBuilder::bind_resources_to_descriptors() {
        ID3D12Device* device = render_device->get_d3d12_device();

        std::vector<BoundResource<D3D12Image>> used_images;
        std::vector<BoundResource<D3D12Buffer>> used_buffers;

        for(const auto& [name, descriptor] : descriptors) {
            if(const auto& buffer_slot = bound_buffers.find(name); buffer_slot != bound_buffers.end()) {
                const auto* buffer = buffer_slot->second;
                switch(descriptor.type) {
                    case D3D12Descriptor::Type::CBV: {
                        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{buffer->resource->GetGPUVirtualAddress(), static_cast<UINT>(buffer->size)};

                        device->CreateConstantBufferView(&desc, descriptor.handle);

                        used_buffers.emplace_back(buffer,
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                      D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                    } break;

                    case D3D12Descriptor::Type::SRV: {
                        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
                        desc.Format = DXGI_FORMAT_R8_UINT; // TODO: Figure out if that's correct
                        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        desc.Buffer.FirstElement = 0;
                        desc.Buffer.NumElements = descriptor.num_elements;
                        desc.Buffer.StructureByteStride = descriptor.element_size;

                        device->CreateShaderResourceView(buffer->resource.Get(), &desc, descriptor.handle);

                        used_buffers.emplace_back(buffer,
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    } break;

                    case D3D12Descriptor::Type::UAV: {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
                        desc.Format = DXGI_FORMAT_R8_UINT; // TODO: Figure this out
                        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        desc.Buffer.FirstElement = 0;
                        desc.Buffer.NumElements = descriptor.num_elements;
                        desc.Buffer.StructureByteStride = descriptor.element_size;

                        device->CreateUnorderedAccessView(buffer->resource.Get(), nullptr, &desc, descriptor.handle);

                        used_buffers.emplace_back(buffer,
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    } break;
                }

            } else if(const auto& images_slot = bound_images.find(name); images_slot != bound_images.end()) {
                const auto& images = images_slot->second;
                auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{descriptor.handle};

                ENSURE(descriptor.type != D3D12Descriptor::Type::CBV, "Can not bind a texture to constant buffer variable {}", name);

                ENSURE(!images.empty(), "Can not bind an empty image array to variable {}", name);

                if(descriptor.type == D3D12Descriptor::Type::SRV) {
                    for(const auto* image : images) {
                        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
                        desc.Format = image->format;
                        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // TODO: Figure out how to support other texture types
                        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        desc.Texture2D.MostDetailedMip = 0;
                        desc.Texture2D.MipLevels = 0xFFFFFFFF;
                        desc.Texture2D.PlaneSlice = 0;
                        desc.Texture2D.ResourceMinLODClamp = 0;

                        device->CreateShaderResourceView(image->resource.Get(), &desc, handle);
                        handle.Offset(render_device->get_shader_resource_descriptor_size());

                        used_images.emplace_back(image,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    }

                } else if(descriptor.type == D3D12Descriptor::Type::UAV) {
                    for(const auto* image : images) {
                        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
                        desc.Format = image->format;
                        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        desc.Texture2D.MipSlice = 0;
                        desc.Texture2D.PlaneSlice = 0;

                        device->CreateUnorderedAccessView(image->resource.Get(), nullptr, &desc, handle);
                        handle.Offset(render_device->get_shader_resource_descriptor_size());

                        used_images.emplace_back(image,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    }
                }

            } else {
                ENSURE(false, "No resource bound for variable {}", name);
            }
        }

        return {used_images, used_buffers};
    }
} // namespace render
