/*
 * This file contains convenience initializers for some frequently used Vulkan structs that take many lines to initialize.
 */

#ifndef VKENG_VK_INITIALIZERS_H
#define VKENG_VK_INITIALIZERS_H


#include "vk_types.h"

namespace vkinit {
    vk::PipelineShaderStageCreateInfo
    pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits stage, vk::ShaderModule shaderModule);

    vk::CommandPoolCreateInfo
    commandPoolCreateInfo(uint32_t graphicsFamilyIndex, vk::CommandPoolCreateFlags createFlags);

    vk::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo();

    vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology topology);

    vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo(vk::PolygonMode polygonMode);

    vk::PipelineMultisampleStateCreateInfo multisampleStateCreateInfo(vk::SampleCountFlagBits msaaSamples);

    vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState();

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo();

    vk::ImageCreateInfo imageCreateInfo(vk::Format format, vk::ImageUsageFlags usageFlags, vk::Extent3D extent, vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1);

    vk::ImageViewCreateInfo imageViewCreateInfo(vk::Format format, vk::Image image, vk::ImageAspectFlags aspectFlags);

    vk::PipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo(bool depthTest, bool depthWrite, vk::CompareOp compareOp);

    vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding(vk::DescriptorType type, vk::ShaderStageFlags flags, uint32_t binding);

    vk::WriteDescriptorSet writeDescriptorSet(vk::DescriptorType type, vk::DescriptorSet dstSet, vk::DescriptorBufferInfo* bufferInfo, uint32_t binding);

    vk::WriteDescriptorSet writeDescriptorSet(vk::DescriptorType type, vk::DescriptorSet dstSet, vk::DescriptorImageInfo* imageInfo, uint32_t binding);

    vk::SamplerCreateInfo samplerCreateInfo(vk::Filter filters, vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat);


}


#endif //VKENG_VK_INITIALIZERS_H
