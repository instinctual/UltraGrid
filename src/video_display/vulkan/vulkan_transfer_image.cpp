/**
 * @file   video_display/vulkan/vulkan_transfer_image.cpp
 * @author Martin Bela      <492789@mail.muni.cz>
 */
/*
 * Copyright (c) 2021-2023 CESNET, z. s. p. o.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vulkan_transfer_image.hpp"

#include <cstdlib>

using namespace vulkan_display_detail;
using namespace vulkan_display;

namespace {

/*constexpr vk::DeviceSize add_padding(vk::DeviceSize size, vk::DeviceSize allignment) {
        vk::DeviceSize remainder = size % allignment;
        if (remainder == 0) {
                return size;
        }
        return size + allignment - remainder;
}*/


/**
 * Check if the required flags are present among the provided flags
 */
template<typename T>
constexpr bool flags_present(T provided_flags, T required_flags) {
        return (provided_flags & required_flags) == required_flags;
}

uint32_t get_memory_type(
        uint32_t memory_type_bits,
        vk::MemoryPropertyFlags requested_properties, vk::MemoryPropertyFlags optional_properties,
        vk::PhysicalDevice gpu)
{
        uint32_t possible_memory_type = UINT32_MAX;
        auto supported_properties = gpu.getMemoryProperties();
        for (uint32_t i = 0; i < supported_properties.memoryTypeCount; i++) {
                // if i-th bit in memory_type_bits is set, than i-th memory type can be used
                bool is_type_usable = (1u << i) & memory_type_bits;
                auto& mem_type = supported_properties.memoryTypes[i];
                if (flags_present(mem_type.propertyFlags, requested_properties) && is_type_usable) {
                        if (flags_present(mem_type.propertyFlags, optional_properties)) {
                                return i;
                        }
                        possible_memory_type = i;
                }
        }
        if (possible_memory_type != UINT32_MAX) {
                return possible_memory_type;
        }
        throw VulkanError{"No available memory for transfer images found."};
}

} //namespace -------------------------------------------------------------

namespace vulkan_display_detail{

vk::Extent2D get_buffer_size(const vulkan_display::ImageDescription& description){
        if (description.format == vulkan_display::Format::UYVY8_422_conv){
                return { description.size.width / 2, description.size.height };
        }
        return description.size;
}

void TransferImageImpl::init(vk::Device device, uint32_t id) {
        this->id = id;
        vk::FenceCreateInfo fence_info{};
        is_available_fence = device.createFence(fence_info);
}

void Image2D::init(VulkanContext& context,
        vk::Extent2D size, vk::Format format, vk::ImageUsageFlags usage, 
        vk::AccessFlags initial_access, InitialImageData preinitialised, MemoryLocation memory_location)
{
        vk::ImageTiling tiling;
        vk::MemoryPropertyFlags requested_properties;
        vk::MemoryPropertyFlags optional_properties;
        
        using MemBits = vk::MemoryPropertyFlagBits;
        if (memory_location == MemoryLocation::host_local){
                tiling = vk::ImageTiling::eLinear;
                requested_properties = MemBits::eHostVisible | MemBits::eHostCoherent;
                optional_properties = MemBits::eHostCached;
        }
        else{
                tiling = vk::ImageTiling::eOptimal;
                requested_properties = {};
                optional_properties = MemBits::eDeviceLocal;
        }
        this->init(context, size, format, usage, initial_access, preinitialised, tiling, requested_properties, optional_properties);
}

void Image2D::init(VulkanContext& context, vk::Extent2D size, vk::Format format, vk::ImageUsageFlags usage,
        vk::AccessFlags initial_access, InitialImageData preinitialised, vk::ImageTiling tiling,
        vk::MemoryPropertyFlags requested_properties, vk::MemoryPropertyFlags optional_properties)
{
        this->format = format;
        this->size = size;
        this->access = initial_access;
        this->layout = preinitialised == InitialImageData::preinitialised ? 
                vk::ImageLayout::ePreinitialized : 
                vk::ImageLayout::eUndefined;
        this->view = nullptr;

        vk::Device device = context.get_device();
        vk::ImageCreateInfo image_info{};
        image_info
                .setImageType(vk::ImageType::e2D)
                .setExtent(vk::Extent3D{ size, 1 })
                .setMipLevels(1)
                .setArrayLayers(1)
                .setFormat(format)
                .setTiling(tiling)
                .setInitialLayout(layout)
                .setUsage(usage)
                .setSharingMode(vk::SharingMode::eExclusive)
                .setSamples(vk::SampleCountFlagBits::e1);
        image = device.createImage(image_info);

        vk::MemoryRequirements memory_requirements = device.getImageMemoryRequirements(image);
        byte_size = memory_requirements.size;

        uint32_t memory_type = get_memory_type(memory_requirements.memoryTypeBits,
                requested_properties, optional_properties, context.get_gpu());

        vk::MemoryAllocateInfo allocInfo{ byte_size , memory_type };
        memory = device.allocateMemory(allocInfo);

        device.bindImageMemory(image, memory, 0);
}

bool Image2D::init_external_host(
        VulkanContext& context, vk::Extent2D requested_size,
        vk::Format requested_format, vk::ImageUsageFlags usage,
        vk::AccessFlags initial_access, InitialImageData preinitialised)
{
        if (!context.is_external_host_memory_supported()) {
                return false;
        }

        VkDevice device = context.get_device();
        VkPhysicalDevice physical = context.get_gpu();
        auto get_host_properties =
            reinterpret_cast<PFN_vkGetMemoryHostPointerPropertiesEXT>(
                vkGetDeviceProcAddr(
                    device, "vkGetMemoryHostPointerPropertiesEXT"));
        if (get_host_properties == nullptr) {
                return false;
        }

        VkPhysicalDeviceExternalMemoryHostPropertiesEXT host_properties{};
        host_properties.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &host_properties;
        vkGetPhysicalDeviceProperties2(physical, &properties);
        const VkDeviceSize host_alignment =
            host_properties.minImportedHostPointerAlignment;
        if (host_alignment == 0) {
                return false;
        }

        VkExternalMemoryImageCreateInfo external{};
        external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.handleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.pNext = &external;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.extent = {requested_size.width, requested_size.height, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.format = static_cast<VkFormat>(requested_format);
        image_info.tiling = VK_IMAGE_TILING_LINEAR;
        image_info.initialLayout =
            preinitialised == InitialImageData::preinitialised
                ? VK_IMAGE_LAYOUT_PREINITIALIZED
                : VK_IMAGE_LAYOUT_UNDEFINED;
        image_info.usage = static_cast<VkImageUsageFlags>(usage);
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;

        VkImage external_image{};
        if (vkCreateImage(device, &image_info, nullptr, &external_image) !=
            VK_SUCCESS) {
                return false;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, external_image, &requirements);
        const VkDeviceSize alignment =
            std::max(host_alignment, requirements.alignment);
        const VkDeviceSize allocation_size =
            (requirements.size + alignment - 1) / alignment * alignment;
        void *host_ptr = nullptr;
        if (posix_memalign(&host_ptr, static_cast<size_t>(alignment),
                           static_cast<size_t>(allocation_size)) != 0) {
                vkDestroyImage(device, external_image, nullptr);
                return false;
        }

        VkMemoryHostPointerPropertiesEXT pointer_properties{};
        pointer_properties.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT;
        if (get_host_properties(
                device,
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
                host_ptr, &pointer_properties) != VK_SUCCESS) {
                std::free(host_ptr);
                vkDestroyImage(device, external_image, nullptr);
                return false;
        }

        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
        const uint32_t usable_types =
            requirements.memoryTypeBits & pointer_properties.memoryTypeBits;
        uint32_t memory_type_index = UINT32_MAX;
        constexpr VkMemoryPropertyFlags required_flags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
                if ((usable_types & (1U << i)) != 0U &&
                    (memory_properties.memoryTypes[i].propertyFlags &
                     required_flags) == required_flags) {
                        memory_type_index = i;
                        break;
                }
        }
        if (memory_type_index == UINT32_MAX) {
                std::free(host_ptr);
                vkDestroyImage(device, external_image, nullptr);
                return false;
        }

        VkImportMemoryHostPointerInfoEXT import{};
        import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT;
        import.handleType =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
        import.pHostPointer = host_ptr;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.pNext = &import;
        allocation.allocationSize = allocation_size;
        allocation.memoryTypeIndex = memory_type_index;
        VkDeviceMemory external_memory{};
        if (vkAllocateMemory(device, &allocation, nullptr, &external_memory) !=
                VK_SUCCESS ||
            vkBindImageMemory(device, external_image, external_memory, 0) !=
                VK_SUCCESS) {
                if (external_memory != VK_NULL_HANDLE) {
                        vkFreeMemory(device, external_memory, nullptr);
                }
                std::free(host_ptr);
                vkDestroyImage(device, external_image, nullptr);
                return false;
        }

        image = external_image;
        memory = external_memory;
        external_host_ptr = host_ptr;
        byte_size = static_cast<size_t>(allocation_size);
        size = requested_size;
        format = requested_format;
        access = initial_access;
        layout = preinitialised == InitialImageData::preinitialised
                     ? vk::ImageLayout::ePreinitialized
                     : vk::ImageLayout::eUndefined;
        return true;
}

vk::ImageView Image2D::get_image_view(vk::Device device, vk::SamplerYcbcrConversion conversion) {
        if(!view){
                assert(image);
                vk::ImageViewCreateInfo view_info =
                        default_image_view_create_info(format);
                view_info.setImage(image);

                vk::SamplerYcbcrConversionInfo yCbCr_info{ conversion };
                view_info.setPNext(conversion ? &yCbCr_info : nullptr);
                view = device.createImageView(view_info);
        }
        return view;
}

void Image2D::destroy(vk::Device device) {
        device.destroy(view);
        view = nullptr;
        device.destroy(image);
        image = nullptr;

        if (memory) {
                device.freeMemory(memory);
        }
        if (external_host_ptr) {
                std::free(external_host_ptr);
                external_host_ptr = nullptr;
        }
}

void TransferImageImpl::recreate(VulkanContext& context, ImageDescription description) {
        assert(id != NO_ID);
        buffer.destroy(context.get_device());
        
        auto device = context.get_device();

        const auto buffer_size = get_buffer_size(description);
        const auto buffer_format = description.format_info().buffer_format;
        if (buffer.init_external_host(
                context, buffer_size, buffer_format,
                vk::ImageUsageFlagBits::eSampled,
                vk::AccessFlagBits::eHostWrite,
                InitialImageData::preinitialised)) {
                ptr = static_cast<unsigned char *>(buffer.external_host_ptr);
        } else {
                buffer.init(
                    context, buffer_size, buffer_format,
                    vk::ImageUsageFlagBits::eSampled,
                    vk::AccessFlagBits::eHostWrite,
                    InitialImageData::preinitialised,
                    MemoryLocation::host_local);

                void* void_ptr =
                    device.mapMemory(buffer.memory, 0, buffer.byte_size);
                if (void_ptr == nullptr) {
                        throw VulkanError{"Image memory cannot be mapped."};
                }
                ptr = static_cast<unsigned char *>(void_ptr);
        }

        vk::ImageSubresource subresource{ vk::ImageAspectFlagBits::eColor, 0, 0 };
        row_pitch = device.getImageSubresourceLayout(buffer.image, subresource).rowPitch;

        image_description = description;
}

vk::ImageMemoryBarrier  Image2D::create_memory_barrier(
        vk::ImageLayout new_layout, vk::AccessFlags new_access_mask,
        uint32_t src_queue_family_index, uint32_t dst_queue_family_index)
{
        vk::ImageMemoryBarrier memory_barrier{};
        memory_barrier
                .setImage(image)
                .setOldLayout(layout)
                .setNewLayout(new_layout)
                .setSrcAccessMask(access)
                .setDstAccessMask(new_access_mask)
                .setSrcQueueFamilyIndex(src_queue_family_index)
                .setDstQueueFamilyIndex(dst_queue_family_index);
        memory_barrier.subresourceRange
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setLayerCount(1)
                .setLevelCount(1);

        layout = new_layout;
        access = new_access_mask;
        return memory_barrier;
}

void TransferImageImpl::preprocess() {
        if (preprocess_fun) {
                vulkan_display::TransferImage img{ *this };
                preprocess_fun(img);
                img.set_process_function(nullptr);
        }
}

void TransferImageImpl::destroy(vk::Device device) {
        if (!buffer.external_host_ptr) {
                device.unmapMemory(buffer.memory);
        }
        buffer.destroy(device);
        device.destroy(is_available_fence);
}

} //vulkan_display_detail
