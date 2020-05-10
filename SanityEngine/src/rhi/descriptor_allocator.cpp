#include "descriptor_allocator.hpp"

#include "d3dx12.hpp"

using std::move;

namespace rhi {
    DescriptorAllocator::DescriptorAllocator(ComPtr<ID3D12DescriptorHeap> heap_in, const UINT descriptor_size_in)
        : heap{move(heap_in)}, descriptor_size{descriptor_size_in} {}

    DescriptorAllocator::DescriptorAllocator(DescriptorAllocator&& old) noexcept
        : heap{move(old.heap)},
          descriptor_size{old.descriptor_size},
          next_free_descriptor{old.next_free_descriptor},
          available_handles{move(old.available_handles)} {}

    DescriptorAllocator& DescriptorAllocator::operator=(DescriptorAllocator&& old) noexcept {
        heap = move(old.heap);
        descriptor_size = old.descriptor_size;
        next_free_descriptor = old.next_free_descriptor;
        available_handles = move(old.available_handles);

        return *this;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::get_next_free_descriptor() {
        if(!available_handles.empty()) {
            const auto handle = *available_handles.end();
            available_handles.pop_back();

            return handle;
        }

        const auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE{heap->GetCPUDescriptorHandleForHeapStart(),
                                                          next_free_descriptor,
                                                          descriptor_size};
        next_free_descriptor++;
        return handle;
    }

    void DescriptorAllocator::return_descriptor(const D3D12_CPU_DESCRIPTOR_HANDLE handle) { available_handles.push_back(handle); }
} // namespace render