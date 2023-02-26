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

    vk::PipelineMultisampleStateCreateInfo multisampleStateCreateInfo();

    vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState();

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo();

    vk::ImageCreateInfo imageCreateInfo(vk::Format format, vk::ImageUsageFlags usageFlags, vk::Extent3D extent);

    vk::ImageViewCreateInfo imageViewCreateInfo(vk::Format format, vk::Image image, vk::ImageAspectFlags aspectFlags);

    vk::PipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo(bool depthTest, bool depthWrite, vk::CompareOp compareOp);
}


#endif //VKENG_VK_INITIALIZERS_H
