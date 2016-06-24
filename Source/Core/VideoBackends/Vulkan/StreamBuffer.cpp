// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cassert>

#include "VideoBackends/Vulkan/CommandBufferManager.h"
#include "VideoBackends/Vulkan/ObjectCache.h"
#include "VideoBackends/Vulkan/StreamBuffer.h"

namespace Vulkan {

static size_t AlignBufferOffset(size_t offset, size_t alignment)
{
	// Assume an offset of zero is already aligned to a value larger than alignment.
	if (offset == 0)
		return 0;

	// Have to use divide/multiply here in case alignment is not a power of two.
	// TODO: Can we make this assumption?
	return (offset + (alignment - 1)) / alignment * alignment;
}

StreamBuffer::StreamBuffer(ObjectCache* object_cache, CommandBufferManager* command_buffer_mgr, VkBufferUsageFlags usage, size_t max_size)
	: m_object_cache(object_cache)
	, m_command_buffer_mgr(command_buffer_mgr)
	, m_usage(usage)
	, m_current_size(0)
	, m_maximum_size(max_size)
	, m_current_offset(0)
	, m_current_gpu_position(0)
	, m_last_allocation_size(0)
	, m_buffer(VK_NULL_HANDLE)
	, m_memory(VK_NULL_HANDLE)
	, m_host_pointer(nullptr)
{

}

StreamBuffer::~StreamBuffer()
{
	if (m_host_pointer)
		vkUnmapMemory(m_object_cache->GetDevice(), m_memory);

	if (m_buffer != VK_NULL_HANDLE)
		m_command_buffer_mgr->DeferResourceDestruction(m_buffer);
	if (m_memory != VK_NULL_HANDLE)
		m_command_buffer_mgr->DeferResourceDestruction(m_memory);
}

std::unique_ptr<StreamBuffer> StreamBuffer::Create(ObjectCache* object_cache, CommandBufferManager* command_buffer_mgr, VkBufferUsageFlags usage, size_t initial_size, size_t max_size)
{
	std::unique_ptr<StreamBuffer> buffer = std::make_unique<StreamBuffer>(object_cache, command_buffer_mgr, usage, max_size);

	// TODO: Use maximum size until buffer growing/fences are implemented
	if (!buffer->ResizeBuffer(max_size))
		return nullptr;

	return buffer;
}

bool StreamBuffer::ResizeBuffer(size_t size)
{
	// Create the buffer descriptor
	VkBufferCreateInfo buffer_create_info = {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,			// VkStructureType        sType
		nullptr,										// const void*            pNext
		0,												// VkBufferCreateFlags    flags
		static_cast<VkDeviceSize>(size),				// VkDeviceSize           size
		m_usage,										// VkBufferUsageFlags     usage
		VK_SHARING_MODE_EXCLUSIVE,						// VkSharingMode          sharingMode
		0,												// uint32_t               queueFamilyIndexCount
		nullptr											// const uint32_t*        pQueueFamilyIndices
	};

	VkBuffer buffer = VK_NULL_HANDLE;
	VkResult res = vkCreateBuffer(m_object_cache->GetDevice(), &buffer_create_info, nullptr, &buffer);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkCreateBuffer failed: ");
		return false;
	}

	// Get memory requirements (types etc) for this buffer
	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(m_object_cache->GetDevice(), buffer, &memory_requirements);

	// TODO: Support non-coherent mapping by use of vkFlushMappedMemoryRanges
	uint32_t memory_type_index = m_object_cache->GetMemoryType(memory_requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// Allocate memory for backing this buffer
	VkMemoryAllocateInfo memory_allocate_info = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,			// VkStructureType    sType
		nullptr,										// const void*        pNext
		memory_requirements.size,						// VkDeviceSize       allocationSize
		memory_type_index								// uint32_t           memoryTypeIndex
	};
	VkDeviceMemory memory = VK_NULL_HANDLE;
	res = vkAllocateMemory(m_object_cache->GetDevice(), &memory_allocate_info, nullptr, &memory);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkAllocateMemory failed: ");
		vkDestroyBuffer(m_object_cache->GetDevice(), buffer, nullptr);
		return false;
	}

	// Bind memory to buffer
	res = vkBindBufferMemory(m_object_cache->GetDevice(), buffer, memory, 0);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkBindBufferMemory failed: ");
		vkDestroyBuffer(m_object_cache->GetDevice(), buffer, nullptr);
		vkFreeMemory(m_object_cache->GetDevice(), memory, nullptr);
		return false;
	}

	// Map this buffer into user-space
	void* mapped_ptr = nullptr;
	res = vkMapMemory(m_object_cache->GetDevice(), memory, 0, size, 0, &mapped_ptr);
	if (res != VK_SUCCESS)
	{
		LOG_VULKAN_ERROR(res, "vkMapMemory failed: ");
		vkDestroyBuffer(m_object_cache->GetDevice(), buffer, nullptr);
		vkFreeMemory(m_object_cache->GetDevice(), memory, nullptr);
		return false;
	}

	// Unmap current host pointer (if there was a previous buffer)
	if (m_host_pointer)
		vkUnmapMemory(m_object_cache->GetDevice(), m_memory);

	// Destroy the backings for the buffer after the command buffer executes
	if (m_buffer != VK_NULL_HANDLE)
		m_command_buffer_mgr->DeferResourceDestruction(m_buffer);
	if (m_memory != VK_NULL_HANDLE)
		m_command_buffer_mgr->DeferResourceDestruction(m_memory);

	// Replace with the new buffer
	m_buffer = buffer;
	m_memory = memory;
	m_host_pointer = reinterpret_cast<u8*>(mapped_ptr);
	m_current_size = size;
	m_current_offset = 0;
	m_current_gpu_position = 0;
	return true;
}

bool StreamBuffer::ReserveMemory(size_t num_bytes, size_t alignment)
{
	size_t required_bytes = num_bytes + alignment;

	// Check for space past the current gpu position
	if (m_current_offset >= m_current_gpu_position)
	{
		size_t remaining_bytes = m_current_size - m_current_offset;
		if (required_bytes <= remaining_bytes)
		{
			m_current_offset = AlignBufferOffset(m_current_offset, alignment);
			m_last_allocation_size = num_bytes;
			return true;
		}

		// Check for space at the start of the buffer
		if (required_bytes <= m_current_gpu_position)
		{
			// Reset offset to zero, since we're allocating behind the gpu now
			m_current_offset = 0;
			m_last_allocation_size = num_bytes;
			return true;
		}
	}

	// Are we behind the gpu?
	if (m_current_offset < m_current_gpu_position)
	{
		size_t remaining_bytes = m_current_gpu_position - m_current_offset;
		if (required_bytes <= remaining_bytes)
		{
			// Put after the current allocation but before the gpu
			m_current_offset = AlignBufferOffset(m_current_offset, alignment);
			m_last_allocation_size = num_bytes;
			return true;
		}
	}

	// TODO: Try waiting for fences
	// TODO: Grow the buffer
	return false;
}

void StreamBuffer::CommitMemory(size_t final_num_bytes)
{
	assert((m_current_offset + final_num_bytes) <= m_current_size);
	assert(final_num_bytes <= m_last_allocation_size);
	m_current_offset += final_num_bytes;

	// TODO: For non-coherent mappings, flush the memory range
}


}		// namespace Vulkan
