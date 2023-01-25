#include "vk_initializers.h"

//Global dispatch loader singleton
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

vk::PipelineShaderStageCreateInfo vkinit::pipelineShaderStageCreateInfo(vk::ShaderStageFlagBits stage, vk::ShaderModule shaderModule) {
    vk::PipelineShaderStageCreateInfo info = {};

    //Shader stage; this has to be only one bitflag, not a combination.
    info.stage = stage;
    //Module containing the shader code for this stage
    info.module = shaderModule;
    //Entry point of the shader code.
    info.setPName("main");
    return info;
}

vk::PipelineVertexInputStateCreateInfo vkinit::pipelineVertexInputStateCreateInfo() {
    vk::PipelineVertexInputStateCreateInfo info = {};

    //None of this for now
    info.vertexBindingDescriptionCount = 0;
    info.vertexAttributeDescriptionCount = 0;
    return info;
}

vk::PipelineInputAssemblyStateCreateInfo vkinit::pipelineInputAssemblyStateCreateInfo(vk::PrimitiveTopology topology) {
    vk::PipelineInputAssemblyStateCreateInfo info = {};

    //Topology might be e.g. triangle, line, triangle-list etc.
    info.topology = topology;
    //Tutorial says we aren't using this for the entire tutorial. What does it do? Maybe I'll find out eventually!!
    info.primitiveRestartEnable = VK_FALSE;

    return info;
}

//This is where we set e.g. backface culling etc.
vk::PipelineRasterizationStateCreateInfo vkinit::pipelineRasterizationStateCreateInfo(vk::PolygonMode polygonMode) {
    vk::PipelineRasterizationStateCreateInfo info = {};

    info.depthClampEnable = VK_FALSE;
    //If enabled, discards primitives before rasterization stage -- which we don't want
    //Quote: "You might enable this, for example, if you’re only interested in the side effects of the vertex processing stages,
    //such as writing to a buffer which you later read from. But in our case we’re interested in drawing the triangle, so we leave it disabled."
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = polygonMode;
    info.lineWidth = 1.0f;
    //Disable backface culling
    info.cullMode = vk::CullModeFlagBits::eNone;
    info.frontFace = vk::FrontFace::eClockwise;
    info.depthBiasEnable = VK_FALSE;
    info.depthBiasConstantFactor = 0.0f;
    info.depthBiasClamp = 0.0f;
    info.depthBiasSlopeFactor = 0.0f;

    return info;
}

//We don't use MSAA for now
vk::PipelineMultisampleStateCreateInfo vkinit::multisampleStateCreateInfo() {
    vk::PipelineMultisampleStateCreateInfo info = {};

    info.sampleShadingEnable = VK_FALSE;
    info.rasterizationSamples = vk::SampleCountFlagBits::e1;
    info.minSampleShading = 1.0f;
    info.alphaToCoverageEnable = VK_FALSE;
    info.alphaToOneEnable = VK_FALSE;

    return info;
}

vk::PipelineColorBlendAttachmentState vkinit::pipelineColorBlendAttachmentState() {
    vk::PipelineColorBlendAttachmentState state = {};
    state.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    state.blendEnable = VK_FALSE;

    return state;
}

//Quote: "Pipeline layouts contain the information about shader inputs of a given pipeline.
// It’s here where you would configure your push-constants and descriptor sets, but at the time we won’t need it,
// so we are going to create an empty pipeline layout for our Pipeline"
vk::PipelineLayoutCreateInfo vkinit::pipelineLayoutCreateInfo() {
    vk::PipelineLayoutCreateInfo info = {};

    //empty defaults -- we don't actually need to zero-initialize these with the C++ bindings so I'm just
    //putting them in a comment for posterity. We will use them soon.
    /*
    info.flags = 0;
    info.setLayoutCount = 0;
    info.pSetLayouts = nullptr;
    info.pushConstantRangeCount = 0;
    info.pPushConstantRanges = nullptr;
    */

    return info;
}
