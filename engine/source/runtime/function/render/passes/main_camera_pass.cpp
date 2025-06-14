#include "runtime/function/render/passes/main_camera_pass.h"
#include "runtime/function/render/render_helper.h"
#include "runtime/function/render/render_mesh.h"

#include "runtime/function/render/interface/vulkan/vulkan_rhi.h"
#include "runtime/function/render/interface/vulkan/vulkan_util.h"

#include <stdexcept>

#include <axis_frag.h>
#include <axis_vert.h>
#include <deferred_lighting_frag.h>
#include <deferred_lighting_vert.h>
#include <mesh_frag.h>
#include <mesh_gbuffer_frag.h>
#include <mesh_vert.h>
#include <skybox_frag.h>
#include <skybox_vert.h>

// TODO: render pipeline as asset (not hard code)
// TODO: Cancelable (configurable) subpass

namespace Piccolo {
void MainCameraPass::initialize(const RenderPassInitInfo* init_info) {
    RenderPass::initialize(nullptr);

    const MainCameraPassInitInfo* _init_info = static_cast<const MainCameraPassInitInfo*>(init_info);
    m_enable_fxaa                            = _init_info->enable_fxaa;

    setupAttachments();
    setupRenderPass();
    setupDescriptorSetLayouts();
    setupPipelines();
    setupDescriptorSets();
    setupFramebufferDescriptorSet();
    setupSwapchainFramebuffers();

    setupParticlePass();
}

void MainCameraPass::preparePassData(std::shared_ptr<RenderResourceBase> render_resource) {
    const RenderResource* vulkan_resource = static_cast<const RenderResource*>(render_resource.get());
    if (vulkan_resource) {
        m_mesh_perframe_storage_buffer_object = vulkan_resource->m_mesh_perframe_storage_buffer_object;
        m_axis_storage_buffer_object          = vulkan_resource->m_axis_storage_buffer_object;
    }
}

void MainCameraPass::setupAttachments() {
    m_framebuffer.attachments.resize(_main_camera_pass_custom_attachment_count +
                                     _main_camera_pass_post_process_attachment_count);

    m_framebuffer.attachments[_main_camera_pass_gbuffer_a].format          = RHI_FORMAT_R8G8B8A8_UNORM;
    m_framebuffer.attachments[_main_camera_pass_gbuffer_b].format          = RHI_FORMAT_R8G8B8A8_UNORM;
    m_framebuffer.attachments[_main_camera_pass_gbuffer_c].format          = RHI_FORMAT_R8G8B8A8_SRGB;
    m_framebuffer.attachments[_main_camera_pass_backup_buffer_a].format  = RHI_FORMAT_R16G16B16A16_SFLOAT;
    m_framebuffer.attachments[_main_camera_pass_backup_buffer_b].format = RHI_FORMAT_R16G16B16A16_SFLOAT;

    for (int buffer_index = 0; buffer_index < _main_camera_pass_custom_attachment_count; ++buffer_index) {
        if (buffer_index == _main_camera_pass_gbuffer_a) {
            m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                               m_rhi->getSwapchainInfo().extent.height,
                               m_framebuffer.attachments[_main_camera_pass_gbuffer_a].format,
                               RHI_IMAGE_TILING_OPTIMAL,
                               RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | RHI_IMAGE_USAGE_TRANSFER_SRC_BIT,
                               RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               m_framebuffer.attachments[_main_camera_pass_gbuffer_a].image,
                               m_framebuffer.attachments[_main_camera_pass_gbuffer_a].mem,
                               0,
                               1,
                               1);
        } else {
            m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                               m_rhi->getSwapchainInfo().extent.height,
                               m_framebuffer.attachments[buffer_index].format,
                               RHI_IMAGE_TILING_OPTIMAL,
                               RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | RHI_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                               RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               m_framebuffer.attachments[buffer_index].image,
                               m_framebuffer.attachments[buffer_index].mem,
                               0,
                               1,
                               1);
        }
        m_rhi->createImageView(m_framebuffer.attachments[buffer_index].image,
                               m_framebuffer.attachments[buffer_index].format,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_framebuffer.attachments[buffer_index].view);
    }

    m_framebuffer.attachments[_main_camera_pass_post_process_buffer_odd].format  = RHI_FORMAT_R16G16B16A16_SFLOAT;
    m_framebuffer.attachments[_main_camera_pass_post_process_buffer_even].format = RHI_FORMAT_R16G16B16A16_SFLOAT;
    for (int attachment_index = _main_camera_pass_custom_attachment_count;
         attachment_index <
         _main_camera_pass_custom_attachment_count + _main_camera_pass_post_process_attachment_count;
         ++attachment_index) {
        m_rhi->createImage(m_rhi->getSwapchainInfo().extent.width,
                           m_rhi->getSwapchainInfo().extent.height,
                           m_framebuffer.attachments[attachment_index].format,
                           RHI_IMAGE_TILING_OPTIMAL,
                           RHI_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | RHI_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | RHI_IMAGE_USAGE_SAMPLED_BIT,
                           RHI_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           m_framebuffer.attachments[attachment_index].image,
                           m_framebuffer.attachments[attachment_index].mem,
                           0,
                           1,
                           1);
        m_rhi->createImageView(m_framebuffer.attachments[attachment_index].image,
                               m_framebuffer.attachments[attachment_index].format,
                               RHI_IMAGE_ASPECT_COLOR_BIT,
                               RHI_IMAGE_VIEW_TYPE_2D,
                               1,
                               1,
                               m_framebuffer.attachments[attachment_index].view);
    }
}

void MainCameraPass::setupRenderPass() {
    // ----- setupRenderPassAttachments -----
    std::vector<RHIAttachmentDescription> attachments(_main_camera_pass_attachment_count);
    auto setupAttachmentsCommon = [](RHIAttachmentDescription& attachment_descriptor) {
        attachment_descriptor.samples        = RHI_SAMPLE_COUNT_1_BIT;
        attachment_descriptor.loadOp         = RHI_ATTACHMENT_LOAD_OP_CLEAR;
        attachment_descriptor.storeOp        = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_descriptor.stencilLoadOp  = RHI_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment_descriptor.stencilStoreOp = RHI_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment_descriptor.initialLayout  = RHI_IMAGE_LAYOUT_UNDEFINED;
        attachment_descriptor.finalLayout    = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };
    RHIAttachmentDescription &gbuffer_normal_attachment_description = attachments[_main_camera_pass_gbuffer_a];
    setupAttachmentsCommon(gbuffer_normal_attachment_description);
    gbuffer_normal_attachment_description.format  = m_framebuffer.attachments[_main_camera_pass_gbuffer_a].format;
    gbuffer_normal_attachment_description.storeOp = RHI_ATTACHMENT_STORE_OP_STORE;

    RHIAttachmentDescription &gbuffer_metallic_roughness_shadingmodeid_attachment_description = attachments[_main_camera_pass_gbuffer_b];
    setupAttachmentsCommon(gbuffer_metallic_roughness_shadingmodeid_attachment_description);
    gbuffer_metallic_roughness_shadingmodeid_attachment_description.format = m_framebuffer.attachments[_main_camera_pass_gbuffer_b].format;

    RHIAttachmentDescription &gbuffer_albedo_attachment_description = attachments[_main_camera_pass_gbuffer_c];
    setupAttachmentsCommon(gbuffer_albedo_attachment_description);
    gbuffer_albedo_attachment_description.format      = m_framebuffer.attachments[_main_camera_pass_gbuffer_c].format;
    gbuffer_albedo_attachment_description.finalLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIAttachmentDescription &backup_a_color_attachment_description = attachments[_main_camera_pass_backup_buffer_a];
    setupAttachmentsCommon(backup_a_color_attachment_description);
    backup_a_color_attachment_description.format      = m_framebuffer.attachments[_main_camera_pass_backup_buffer_a].format;
    backup_a_color_attachment_description.finalLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIAttachmentDescription &backup_even_color_attachment_description = attachments[_main_camera_pass_backup_buffer_b];
    setupAttachmentsCommon(backup_even_color_attachment_description);
    backup_even_color_attachment_description.format = m_framebuffer.attachments[_main_camera_pass_backup_buffer_b].format;

    RHIAttachmentDescription &post_process_odd_color_attachment_description = attachments[_main_camera_pass_post_process_buffer_odd];
    setupAttachmentsCommon(post_process_odd_color_attachment_description);
    post_process_odd_color_attachment_description.format = m_framebuffer.attachments[_main_camera_pass_post_process_buffer_odd].format;

    RHIAttachmentDescription &post_process_even_color_attachment_description = attachments[_main_camera_pass_post_process_buffer_even];
    setupAttachmentsCommon(post_process_even_color_attachment_description);
    post_process_even_color_attachment_description.format = m_framebuffer.attachments[_main_camera_pass_post_process_buffer_even].format;

    RHIAttachmentDescription &depth_attachment_description = attachments[_main_camera_pass_depth];
    setupAttachmentsCommon(depth_attachment_description);
    depth_attachment_description.format      = m_rhi->getDepthImageInfo().depth_image_format;
    depth_attachment_description.storeOp     = RHI_ATTACHMENT_STORE_OP_STORE;
    depth_attachment_description.finalLayout = RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    RHIAttachmentDescription &swapchain_image_attachment_description = attachments[_main_camera_pass_swap_chain_image];
    setupAttachmentsCommon(swapchain_image_attachment_description);
    swapchain_image_attachment_description.format      = m_rhi->getSwapchainInfo().image_format;
    swapchain_image_attachment_description.storeOp     = RHI_ATTACHMENT_STORE_OP_STORE;
    swapchain_image_attachment_description.finalLayout = RHI_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // ----- setupRenderPassSubpasses -----
    uint32_t post_process_read_buffer = _main_camera_pass_post_process_buffer_even;
    uint32_t post_process_write_buffer = _main_camera_pass_post_process_buffer_odd;
    auto flipPostProcessBuffers = [&]() { std::swap(post_process_read_buffer, post_process_write_buffer); };
    auto setupSubPass = [](RHISubpassDescription &subpass,
                         const std::vector<RHIAttachmentReference>* input_attachments,
                         const std::vector<RHIAttachmentReference>* color_attachments,
                         const std::vector<RHIAttachmentReference>* depth_stencil_attachment,
                         const std::vector<uint32_t>* preserve_attachments) {
        subpass.pipelineBindPoint           = RHI_PIPELINE_BIND_POINT_GRAPHICS;
        if (input_attachments) {
            subpass.inputAttachmentCount    = input_attachments->size();
            subpass.pInputAttachments       = input_attachments->data();
        } else {
            subpass.inputAttachmentCount    = 0;
            subpass.pInputAttachments       = nullptr;
        }
        if (color_attachments) {
            subpass.colorAttachmentCount    = color_attachments->size();
            subpass.pColorAttachments       = color_attachments->data();
        } else {
            subpass.colorAttachmentCount    = 0;
            subpass.pColorAttachments       = nullptr;
        }
        if (depth_stencil_attachment) {
            subpass.pDepthStencilAttachment = depth_stencil_attachment->data();
        } else {
            subpass.pDepthStencilAttachment = nullptr;
        }
        if (preserve_attachments) {
            subpass.preserveAttachmentCount = preserve_attachments->size();
            subpass.pPreserveAttachments    = preserve_attachments->data();
        } else {
            subpass.preserveAttachmentCount = 0;
            subpass.pPreserveAttachments    = nullptr;
        }
    };

    std::vector<RHISubpassDescription> subpasses(_main_camera_subpass_count);
    std::vector<RHIAttachmentReference> base_pass_color_attachments_reference {
        {_main_camera_pass_gbuffer_a, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, // gbuffer_normal_attachment
        {_main_camera_pass_gbuffer_b, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, // gbuffer_metallic_roughness_shadingmodeid_attachment
        {_main_camera_pass_gbuffer_c, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}  // gbuffer_albedo_attachment
    };
    std::vector<RHIAttachmentReference> base_pass_depth_attachment_reference {
        {_main_camera_pass_depth, RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} // depth_attachment_description
    };
    setupSubPass(subpasses[_main_camera_subpass_basepass],
                 nullptr,
                 &base_pass_color_attachments_reference,
                 &base_pass_depth_attachment_reference,
                 nullptr);

    std::vector<RHIAttachmentReference> deferred_lighting_pass_input_attachments_reference {
        {_main_camera_pass_gbuffer_a, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // gbuffer_normal_attachment
        {_main_camera_pass_gbuffer_b, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // gbuffer_metallic_roughness_shadingmodeid_attachment
        {_main_camera_pass_gbuffer_c, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // gbuffer_albedo_attachment
        {_main_camera_pass_depth, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}      // depth_attachment
    };
    std::vector<RHIAttachmentReference> deferred_lighting_pass_color_attachment_reference {
        {_main_camera_pass_backup_buffer_a, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // backup_a_color_attachment
    };
    setupSubPass(subpasses[_main_camera_subpass_deferred_lighting],
                 &deferred_lighting_pass_input_attachments_reference,
                 &deferred_lighting_pass_color_attachment_reference,
                 nullptr,
                 nullptr);

    std::vector<RHIAttachmentReference> forward_lighting_pass_color_attachments_reference {
        {_main_camera_pass_backup_buffer_a, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // backup_a_color_attachment
    };
    std::vector<RHIAttachmentReference> forward_lighting_pass_depth_attachment_reference {
        {_main_camera_pass_depth, RHI_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL} // depth_attachment
    };
    setupSubPass(subpasses[_main_camera_subpass_forward_lighting],
                 nullptr,
                 &forward_lighting_pass_color_attachments_reference,
                 &forward_lighting_pass_depth_attachment_reference,
                 nullptr);

    std::vector<RHIAttachmentReference> tone_mapping_pass_input_attachment_reference {
        {_main_camera_pass_backup_buffer_a, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL} // backup_a_color_attachment
    };
    std::vector<RHIAttachmentReference> tone_mapping_pass_color_attachment_reference {
        {post_process_write_buffer, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // post_odd
    };
    flipPostProcessBuffers();
    setupSubPass(subpasses[_main_camera_subpass_tone_mapping],
                 &tone_mapping_pass_input_attachment_reference,
                 &tone_mapping_pass_color_attachment_reference,
                 nullptr,
                 nullptr);

    std::vector<RHIAttachmentReference> color_grading_pass_input_attachment_reference {
        {post_process_read_buffer, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}  // post_odd
    };
    std::vector<RHIAttachmentReference> color_grading_pass_color_attachment_reference {
        {post_process_write_buffer, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // post_even
    };
    flipPostProcessBuffers();
    setupSubPass(subpasses[_main_camera_subpass_color_grading],
                 &color_grading_pass_input_attachment_reference,
                 &color_grading_pass_color_attachment_reference,
                 nullptr,
                 nullptr);

    std::vector<RHIAttachmentReference> vignette_pass_input_attachment_reference {
        {post_process_read_buffer, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}  // post_even
    };
    std::vector<RHIAttachmentReference> vignette_pass_color_attachment_reference {
        {post_process_write_buffer, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // post_odd
    };
    flipPostProcessBuffers();
    setupSubPass(subpasses[_main_camera_subpass_vignette],
                 &vignette_pass_input_attachment_reference,
                 &vignette_pass_color_attachment_reference,
                 nullptr,
                 nullptr);

    std::vector<RHIAttachmentReference> fxaa_pass_input_attachment_reference {
        {post_process_read_buffer, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}  // post_odd
    };
    std::vector<RHIAttachmentReference> fxaa_pass_color_attachment_reference {
        {post_process_write_buffer, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // post_even
    };
    if (m_enable_fxaa)
        flipPostProcessBuffers();
    setupSubPass(subpasses[_main_camera_subpass_fxaa],
                 &fxaa_pass_input_attachment_reference,
                 &fxaa_pass_color_attachment_reference,
                 nullptr,
                 nullptr);

    std::vector<RHIAttachmentReference> ui_pass_color_attachment_reference {
        {post_process_write_buffer, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // post_odd if enable fxaa
    };
    std::vector<uint32_t> ui_pass_preserve_attachment_reference {
        {post_process_read_buffer} // preserve post_even if enable fxaa
    };
    setupSubPass(subpasses[_main_camera_subpass_ui],
                 nullptr,
                 &ui_pass_color_attachment_reference,
                 nullptr,
                 &ui_pass_preserve_attachment_reference);

    std::vector<RHIAttachmentReference> combine_ui_pass_input_attachments_reference {
        {post_process_read_buffer, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, // post_even if enable fxaa
        {post_process_write_buffer, RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL} // post_odd if enable fxaa (no write actually)
    };
    std::vector<RHIAttachmentReference> combine_ui_pass_color_attachment_reference {
        {_main_camera_pass_swap_chain_image, RHI_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} // swapchain_image_attachment_description
    };
    setupSubPass(subpasses[_main_camera_subpass_combine_ui],
                 &combine_ui_pass_input_attachments_reference,
                 &combine_ui_pass_color_attachment_reference,
                 nullptr,
                 nullptr);

    // ----- setupRenderPassDependencies -----
    std::vector<RHISubpassDependency> dependencies(9);
    auto setCommonMaskAndFlags = [](RHISubpassDependency &dependency) {
        dependency.srcStageMask = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = RHI_ACCESS_SHADER_WRITE_BIT | RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = RHI_ACCESS_SHADER_READ_BIT | RHI_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dependency.dependencyFlags = RHI_DEPENDENCY_BY_REGION_BIT;
    };

    RHISubpassDependency &deferred_lighting_pass_depend_on_shadow_map_pass = dependencies[0];
    deferred_lighting_pass_depend_on_shadow_map_pass.srcSubpass      = RHI_SUBPASS_EXTERNAL;
    deferred_lighting_pass_depend_on_shadow_map_pass.dstSubpass      = _main_camera_subpass_deferred_lighting;
    deferred_lighting_pass_depend_on_shadow_map_pass.srcStageMask    = RHI_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deferred_lighting_pass_depend_on_shadow_map_pass.dstStageMask    = RHI_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deferred_lighting_pass_depend_on_shadow_map_pass.srcAccessMask   = RHI_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deferred_lighting_pass_depend_on_shadow_map_pass.dstAccessMask   = RHI_ACCESS_SHADER_READ_BIT;
    deferred_lighting_pass_depend_on_shadow_map_pass.dependencyFlags = 0; // NOT BY REGION

    RHISubpassDependency &deferred_lighting_pass_depend_on_base_pass = dependencies[1];
    deferred_lighting_pass_depend_on_base_pass.srcSubpass = _main_camera_subpass_basepass;
    deferred_lighting_pass_depend_on_base_pass.dstSubpass = _main_camera_subpass_deferred_lighting;
    setCommonMaskAndFlags(deferred_lighting_pass_depend_on_base_pass);

    RHISubpassDependency &forward_lighting_pass_depend_on_deferred_lighting_pass = dependencies[2];
    forward_lighting_pass_depend_on_deferred_lighting_pass.srcSubpass = _main_camera_subpass_deferred_lighting;
    forward_lighting_pass_depend_on_deferred_lighting_pass.dstSubpass = _main_camera_subpass_forward_lighting;
    setCommonMaskAndFlags(forward_lighting_pass_depend_on_deferred_lighting_pass);

    RHISubpassDependency &tone_mapping_pass_depend_on_lighting_pass = dependencies[3];
    tone_mapping_pass_depend_on_lighting_pass.srcSubpass = _main_camera_subpass_forward_lighting;
    tone_mapping_pass_depend_on_lighting_pass.dstSubpass = _main_camera_subpass_tone_mapping;
    setCommonMaskAndFlags(tone_mapping_pass_depend_on_lighting_pass);

    RHISubpassDependency &color_grading_pass_depend_on_tone_mapping_pass = dependencies[4];
    color_grading_pass_depend_on_tone_mapping_pass.srcSubpass = _main_camera_subpass_tone_mapping;
    color_grading_pass_depend_on_tone_mapping_pass.dstSubpass = _main_camera_subpass_color_grading;
    setCommonMaskAndFlags(color_grading_pass_depend_on_tone_mapping_pass);

    RHISubpassDependency &vignette_pass_depend_on_color_grading_pass = dependencies[5];
    vignette_pass_depend_on_color_grading_pass.srcSubpass = _main_camera_subpass_color_grading;
    vignette_pass_depend_on_color_grading_pass.dstSubpass = _main_camera_subpass_vignette;
    setCommonMaskAndFlags(vignette_pass_depend_on_color_grading_pass);

    RHISubpassDependency &fxaa_pass_depend_on_vignette_pass = dependencies[6];
    fxaa_pass_depend_on_vignette_pass.srcSubpass = _main_camera_subpass_vignette;
    fxaa_pass_depend_on_vignette_pass.dstSubpass = _main_camera_subpass_fxaa;
    setCommonMaskAndFlags(fxaa_pass_depend_on_vignette_pass);

    RHISubpassDependency &ui_pass_depend_on_fxaa_pass = dependencies[7];
    ui_pass_depend_on_fxaa_pass.srcSubpass = _main_camera_subpass_fxaa;
    ui_pass_depend_on_fxaa_pass.dstSubpass = _main_camera_subpass_ui;
    setCommonMaskAndFlags(ui_pass_depend_on_fxaa_pass);

    RHISubpassDependency &combine_ui_pass_depend_on_ui_pass = dependencies[8];
    combine_ui_pass_depend_on_ui_pass.srcSubpass = _main_camera_subpass_ui;
    combine_ui_pass_depend_on_ui_pass.dstSubpass = _main_camera_subpass_combine_ui;
    setCommonMaskAndFlags(combine_ui_pass_depend_on_ui_pass);

    RHIRenderPassCreateInfo renderpass_create_info {};
    renderpass_create_info.sType           = RHI_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderpass_create_info.attachmentCount = attachments.size();
    renderpass_create_info.pAttachments    = attachments.data();
    renderpass_create_info.subpassCount    = subpasses.size();
    renderpass_create_info.pSubpasses      = subpasses.data();
    renderpass_create_info.dependencyCount = dependencies.size();
    renderpass_create_info.pDependencies   = dependencies.data();

    if (m_rhi->createRenderPass(&renderpass_create_info, m_framebuffer.render_pass) != RHI_SUCCESS)
        throw std::runtime_error("failed to create render pass");
}

void MainCameraPass::setupDescriptorSetLayouts() {
    m_descriptor_infos.resize(_layout_type_count);
    setupPerMeshDescriptorSetLayout();
    setupMeshGlobalDescriptorSetLayout();
    setupMeshPerMaterialDescriptorSetLayout();
    setupSkyboxDescriptorSetLayout();
    setupAxisDescriptorSetLayout();
    setupDeferredLightingDescriptorSetLayout();
}

// Per Mesh Layout
void MainCameraPass::setupPerMeshDescriptorSetLayout() {
    std::vector<RHIDescriptorSetLayoutBinding> mesh_mesh_layout_bindings(1);
    mesh_mesh_layout_bindings[0].binding                       = 0;
    mesh_mesh_layout_bindings[0].descriptorType                = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    mesh_mesh_layout_bindings[0].descriptorCount               = 1;
    mesh_mesh_layout_bindings[0].stageFlags                    = RHI_SHADER_STAGE_VERTEX_BIT;
    mesh_mesh_layout_bindings[0].pImmutableSamplers            = NULL;

    RHIDescriptorSetLayoutCreateInfo mesh_mesh_layout_create_info {};
    mesh_mesh_layout_create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    mesh_mesh_layout_create_info.bindingCount = mesh_mesh_layout_bindings.size();
    mesh_mesh_layout_create_info.pBindings    = mesh_mesh_layout_bindings.data();

    if (m_rhi->createDescriptorSetLayout(&mesh_mesh_layout_create_info, m_descriptor_infos[_per_mesh].layout) != RHI_SUCCESS)
        throw std::runtime_error("create mesh mesh layout");
}

// Mesh Global Layout
void MainCameraPass::setupMeshGlobalDescriptorSetLayout() {
    std::vector<RHIDescriptorSetLayoutBinding> mesh_global_layout_bindings(8);
    mesh_global_layout_bindings[0].binding            = 0;
    mesh_global_layout_bindings[0].descriptorType     = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    mesh_global_layout_bindings[0].descriptorCount    = 1;
    mesh_global_layout_bindings[0].stageFlags         = RHI_SHADER_STAGE_VERTEX_BIT | RHI_SHADER_STAGE_FRAGMENT_BIT;
    mesh_global_layout_bindings[0].pImmutableSamplers = NULL;

    mesh_global_layout_bindings[1] = mesh_global_layout_bindings[0];
    mesh_global_layout_bindings[1].binding            = 1;
    mesh_global_layout_bindings[1].stageFlags         = RHI_SHADER_STAGE_VERTEX_BIT;

    mesh_global_layout_bindings[2] = mesh_global_layout_bindings[1];
    mesh_global_layout_bindings[2].binding            = 2;
    mesh_global_layout_bindings[2].stageFlags         = RHI_SHADER_STAGE_VERTEX_BIT;

    mesh_global_layout_bindings[3] = mesh_global_layout_bindings[0];
    mesh_global_layout_bindings[3].binding            = 3;
    mesh_global_layout_bindings[3].descriptorType     = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    mesh_global_layout_bindings[3].stageFlags         = RHI_SHADER_STAGE_FRAGMENT_BIT;

    mesh_global_layout_bindings[4]         = mesh_global_layout_bindings[3];
    mesh_global_layout_bindings[4].binding = 4;

    mesh_global_layout_bindings[5]         = mesh_global_layout_bindings[3];
    mesh_global_layout_bindings[5].binding = 5;  // mesh_global_layout_specular_texture_binding

    mesh_global_layout_bindings[6]         = mesh_global_layout_bindings[3];
    mesh_global_layout_bindings[6].binding = 6;

    mesh_global_layout_bindings[7]         = mesh_global_layout_bindings[3];
    mesh_global_layout_bindings[7].binding = 7; // mesh_global_layout_directional_light_shadow_texture_binding

    RHIDescriptorSetLayoutCreateInfo mesh_global_layout_create_info;
    mesh_global_layout_create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    mesh_global_layout_create_info.pNext        = NULL;
    mesh_global_layout_create_info.flags        = 0;
    mesh_global_layout_create_info.bindingCount = mesh_global_layout_bindings.size();
    mesh_global_layout_create_info.pBindings    = mesh_global_layout_bindings.data();

    if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&mesh_global_layout_create_info, m_descriptor_infos[_mesh_global].layout))
        throw std::runtime_error("create mesh global layout");
}

// Mesh Per Material Layout
void MainCameraPass::setupMeshPerMaterialDescriptorSetLayout() {
    std::vector<RHIDescriptorSetLayoutBinding> mesh_material_layout_bindings(6);
    // mesh_material_layout_uniform_buffer_binding
    mesh_material_layout_bindings[0].binding            = 0; // (set = 2, binding = 0 in fragment shader)
    mesh_material_layout_bindings[0].descriptorType     = RHI_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    mesh_material_layout_bindings[0].descriptorCount    = 1;
    mesh_material_layout_bindings[0].stageFlags         = RHI_SHADER_STAGE_FRAGMENT_BIT;
    mesh_material_layout_bindings[0].pImmutableSamplers = nullptr;

    mesh_material_layout_bindings[1] = mesh_material_layout_bindings[0]; // mesh_material_layout_base_color_texture_binding
    mesh_material_layout_bindings[1].binding         = 1;                // (set = 2, binding = 1 in fragment shader)
    mesh_material_layout_bindings[1].descriptorType  = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    mesh_material_layout_bindings[2] = mesh_material_layout_bindings[1]; // mesh_material_layout_metallic_roughness_texture_binding
    mesh_material_layout_bindings[2].binding = 2;                        // (set = 2, binding = 2 in fragment shader)

    mesh_material_layout_bindings[3] = mesh_material_layout_bindings[1]; // mesh_material_layout_normal_roughness_texture_binding
    mesh_material_layout_bindings[3].binding = 3;                        // (set = 2, binding = 3 in fragment shader)

    mesh_material_layout_bindings[4]         = mesh_material_layout_bindings[1]; // mesh_material_layout_occlusion_texture_binding
    mesh_material_layout_bindings[4].binding = 4;                                // (set = 2, binding = 4 in fragment shader)

    mesh_material_layout_bindings[5]         = mesh_material_layout_bindings[1]; // mesh_material_layout_emissive_texture_binding
    mesh_material_layout_bindings[5].binding = 5;                                // (set = 2, binding = 5 in fragment shader)

    RHIDescriptorSetLayoutCreateInfo mesh_material_layout_create_info;
    mesh_material_layout_create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    mesh_material_layout_create_info.pNext        = NULL;
    mesh_material_layout_create_info.flags        = 0;
    mesh_material_layout_create_info.bindingCount = mesh_material_layout_bindings.size();
    mesh_material_layout_create_info.pBindings    = mesh_material_layout_bindings.data();

    if (m_rhi->createDescriptorSetLayout(&mesh_material_layout_create_info, m_descriptor_infos[_mesh_per_material].layout) != RHI_SUCCESS)
        throw std::runtime_error("create mesh material layout");
}

// skybox layout
void MainCameraPass::setupSkyboxDescriptorSetLayout() {
    std::vector<RHIDescriptorSetLayoutBinding> skybox_layout_bindings(2);
    // skybox_layout_perframe_storage_buffer_binding
    skybox_layout_bindings[0].binding            = 0;
    skybox_layout_bindings[0].descriptorType     = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    skybox_layout_bindings[0].descriptorCount    = 1;
    skybox_layout_bindings[0].stageFlags         = RHI_SHADER_STAGE_VERTEX_BIT;
    skybox_layout_bindings[0].pImmutableSamplers = NULL;

    skybox_layout_bindings[1]                = skybox_layout_bindings[0]; // skybox_layout_specular_texture_binding
    skybox_layout_bindings[1].binding        = 1;
    skybox_layout_bindings[1].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    skybox_layout_bindings[1].stageFlags     = RHI_SHADER_STAGE_FRAGMENT_BIT;

    RHIDescriptorSetLayoutCreateInfo skybox_layout_create_info {};
    skybox_layout_create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    skybox_layout_create_info.bindingCount = skybox_layout_bindings.size();
    skybox_layout_create_info.pBindings    = skybox_layout_bindings.data();

    if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&skybox_layout_create_info, m_descriptor_infos[_skybox].layout))
        throw std::runtime_error("create skybox layout");
}

// axis layout
void MainCameraPass::setupAxisDescriptorSetLayout() {
    std::vector<RHIDescriptorSetLayoutBinding> axis_layout_bindings(2);
    // axis_layout_perframe_storage_buffer_binding
    axis_layout_bindings[0].binding            = 0;
    axis_layout_bindings[0].descriptorType     = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    axis_layout_bindings[0].descriptorCount    = 1;
    axis_layout_bindings[0].stageFlags         = RHI_SHADER_STAGE_VERTEX_BIT;
    axis_layout_bindings[0].pImmutableSamplers = NULL;

    axis_layout_bindings[1]                = axis_layout_bindings[0]; // axis_layout_storage_buffer_binding
    axis_layout_bindings[1].binding        = 1;
    axis_layout_bindings[1].descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    RHIDescriptorSetLayoutCreateInfo axis_layout_create_info {};
    axis_layout_create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    axis_layout_create_info.bindingCount = axis_layout_bindings.size();
    axis_layout_create_info.pBindings    = axis_layout_bindings.data();

    if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&axis_layout_create_info, m_descriptor_infos[_axis].layout))
        throw std::runtime_error("create axis layout");
}

// deferred lighting layout
void MainCameraPass::setupDeferredLightingDescriptorSetLayout() {
    std::vector<RHIDescriptorSetLayoutBinding> gbuffer_lighting_global_layout_bindings(4);
    // gbuffer_normal_global_layout_input_attachment_binding
    gbuffer_lighting_global_layout_bindings[0].binding         = 0;
    gbuffer_lighting_global_layout_bindings[0].descriptorType  = RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    gbuffer_lighting_global_layout_bindings[0].descriptorCount = 1;
    gbuffer_lighting_global_layout_bindings[0].stageFlags      = RHI_SHADER_STAGE_FRAGMENT_BIT;

    gbuffer_lighting_global_layout_bindings[1]         = gbuffer_lighting_global_layout_bindings[0]; // gbuffer_metallic_roughness_shadingmodeid_global_layout_input_attachment_binding
    gbuffer_lighting_global_layout_bindings[1].binding = 1;

    gbuffer_lighting_global_layout_bindings[2]         = gbuffer_lighting_global_layout_bindings[0]; // gbuffer_albedo_global_layout_input_attachment_binding
    gbuffer_lighting_global_layout_bindings[2].binding = 2;

    gbuffer_lighting_global_layout_bindings[3]         = gbuffer_lighting_global_layout_bindings[0]; // gbuffer_depth_global_layout_input_attachment_binding
    gbuffer_lighting_global_layout_bindings[3].binding = 3;

    RHIDescriptorSetLayoutCreateInfo gbuffer_lighting_global_layout_create_info;
    gbuffer_lighting_global_layout_create_info.sType        = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    gbuffer_lighting_global_layout_create_info.pNext        = NULL;
    gbuffer_lighting_global_layout_create_info.flags        = 0;
    gbuffer_lighting_global_layout_create_info.bindingCount = gbuffer_lighting_global_layout_bindings.size();
    gbuffer_lighting_global_layout_create_info.pBindings    = gbuffer_lighting_global_layout_bindings.data();

    if (RHI_SUCCESS != m_rhi->createDescriptorSetLayout(&gbuffer_lighting_global_layout_create_info, m_descriptor_infos[_deferred_lighting].layout))
        throw std::runtime_error("create deferred lighting global layout");
}

void MainCameraPass::setupPipelines() {
    m_render_pipelines.resize(_render_pipeline_type_count);
    setupMeshGBufferPipeline();
    setupDeferredLightingPipeline();
    setupForwardLightingPipeline();
    setupSkyboxPipeline();
    setupAxisPipeline();
}

// mesh gbuffer
void MainCameraPass::setupMeshGBufferPipeline() {
    RHIDescriptorSetLayout *descriptorset_layouts[3] = {m_descriptor_infos[_mesh_global].layout,
                                                        m_descriptor_infos[_per_mesh].layout,
                                                        m_descriptor_infos[_mesh_per_material].layout};
    RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 3;
    pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

    if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[_render_pipeline_type_mesh_gbuffer].layout) != RHI_SUCCESS)
        throw std::runtime_error("create mesh gbuffer pipeline layout");

    RHIShader* vert_shader_module = m_rhi->createShaderModule(MESH_VERT);
    RHIShader* frag_shader_module = m_rhi->createShaderModule(MESH_GBUFFER_FRAG);

    RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
    vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
    vert_pipeline_shader_stage_create_info.module = vert_shader_module;
    vert_pipeline_shader_stage_create_info.pName  = "main";
    // vert_pipeline_shader_stage_create_info.pSpecializationInfo

    RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
    frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
    frag_pipeline_shader_stage_create_info.module = frag_shader_module;
    frag_pipeline_shader_stage_create_info.pName  = "main";

    RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                        frag_pipeline_shader_stage_create_info};

    auto vertex_binding_descriptions   = MeshVertex::getBindingDescriptions();
    auto vertex_attribute_descriptions = MeshVertex::getAttributeDescriptions();
    RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
    vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_create_info.vertexBindingDescriptionCount   = vertex_binding_descriptions.size();
    vertex_input_state_create_info.pVertexBindingDescriptions      = &vertex_binding_descriptions[0];
    vertex_input_state_create_info.vertexAttributeDescriptionCount = vertex_attribute_descriptions.size();
    vertex_input_state_create_info.pVertexAttributeDescriptions    = &vertex_attribute_descriptions[0];

    RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
    input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

    RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
    viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

    RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
    rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
    rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth               = 1.0f;
    rasterization_state_create_info.cullMode                = RHI_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp          = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

    RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
    multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
    multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

    std::vector<RHIPipelineColorBlendAttachmentState> color_blend_attachments(3);
    for (size_t i = 0; i < color_blend_attachments.size(); ++i) {
        color_blend_attachments[i].colorWriteMask      = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                         RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
        color_blend_attachments[i].blendEnable         = RHI_FALSE;
        color_blend_attachments[i].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
        color_blend_attachments[i].dstColorBlendFactor = RHI_BLEND_FACTOR_ZERO;
        color_blend_attachments[i].colorBlendOp        = RHI_BLEND_OP_ADD;
        color_blend_attachments[i].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
        color_blend_attachments[i].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ZERO;
        color_blend_attachments[i].alphaBlendOp        = RHI_BLEND_OP_ADD;
    }

    RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
    color_blend_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_create_info.logicOpEnable     = RHI_FALSE;
    color_blend_state_create_info.logicOp           = RHI_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount   = color_blend_attachments.size();
    color_blend_state_create_info.pAttachments      = color_blend_attachments.data();
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
    depth_stencil_create_info.sType                 = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_create_info.depthTestEnable       = RHI_TRUE;
    depth_stencil_create_info.depthWriteEnable      = RHI_TRUE;
    depth_stencil_create_info.depthCompareOp        = RHI_COMPARE_OP_LESS;
    depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
    depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

    RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
    RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
    dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = 2;
    dynamic_state_create_info.pDynamicStates    = dynamic_states;

    RHIGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shader_stages;
    pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
    pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
    pipelineInfo.pViewportState      = &viewport_state_create_info;
    pipelineInfo.pRasterizationState = &rasterization_state_create_info;
    pipelineInfo.pMultisampleState   = &multisample_state_create_info;
    pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
    pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
    pipelineInfo.layout              = m_render_pipelines[_render_pipeline_type_mesh_gbuffer].layout;
    pipelineInfo.renderPass          = m_framebuffer.render_pass;
    pipelineInfo.subpass             = _main_camera_subpass_basepass;
    pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
    pipelineInfo.pDynamicState       = &dynamic_state_create_info;

    if (RHI_SUCCESS != m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
        1,
        &pipelineInfo,
        m_render_pipelines[_render_pipeline_type_mesh_gbuffer].pipeline))
        throw std::runtime_error("create mesh gbuffer graphics pipeline");

    m_rhi->destroyShaderModule(vert_shader_module);
    m_rhi->destroyShaderModule(frag_shader_module);
}

// deferred lighting
void MainCameraPass::setupDeferredLightingPipeline() {
    RHIDescriptorSetLayout *descriptorset_layouts[3] = {m_descriptor_infos[_mesh_global].layout,
                                                        m_descriptor_infos[_deferred_lighting].layout,
                                                        m_descriptor_infos[_skybox].layout};
    RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = sizeof(descriptorset_layouts) / sizeof(descriptorset_layouts[0]);
    pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

    if (RHI_SUCCESS != m_rhi->createPipelineLayout(&pipeline_layout_create_info,
        m_render_pipelines[_render_pipeline_type_deferred_lighting].layout))
        throw std::runtime_error("create deferred lighting pipeline layout");

    RHIShader* vert_shader_module = m_rhi->createShaderModule(DEFERRED_LIGHTING_VERT);
    RHIShader* frag_shader_module = m_rhi->createShaderModule(DEFERRED_LIGHTING_FRAG);

    RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
    vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
    vert_pipeline_shader_stage_create_info.module = vert_shader_module;
    vert_pipeline_shader_stage_create_info.pName  = "main";
    // vert_pipeline_shader_stage_create_info.pSpecializationInfo

    RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
    frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
    frag_pipeline_shader_stage_create_info.module = frag_shader_module;
    frag_pipeline_shader_stage_create_info.pName  = "main";

    RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                        frag_pipeline_shader_stage_create_info};

    auto vertex_binding_descriptions   = MeshVertex::getBindingDescriptions();
    auto vertex_attribute_descriptions = MeshVertex::getAttributeDescriptions();
    RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
    vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_create_info.vertexBindingDescriptionCount = 0;
    vertex_input_state_create_info.pVertexBindingDescriptions    = NULL;
    vertex_input_state_create_info.vertexBindingDescriptionCount = 0;
    vertex_input_state_create_info.pVertexAttributeDescriptions  = NULL;

    RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
    input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

    RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
    viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

    RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
    rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
    rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth               = 1.0f;
    rasterization_state_create_info.cullMode                = RHI_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp          = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

    RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
    multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
    multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

    std::vector<RHIPipelineColorBlendAttachmentState> color_blend_attachments(1);
    color_blend_attachments[0].colorWriteMask      = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                     RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
    color_blend_attachments[0].blendEnable         = RHI_FALSE;
    color_blend_attachments[0].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].colorBlendOp        = RHI_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].alphaBlendOp        = RHI_BLEND_OP_ADD;

    RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
    color_blend_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_create_info.logicOpEnable     = RHI_FALSE;
    color_blend_state_create_info.logicOp           = RHI_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount   = color_blend_attachments.size();
    color_blend_state_create_info.pAttachments      = color_blend_attachments.data();
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
    depth_stencil_create_info.sType                 = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_create_info.depthTestEnable       = RHI_FALSE;
    depth_stencil_create_info.depthWriteEnable      = RHI_FALSE;
    depth_stencil_create_info.depthCompareOp        = RHI_COMPARE_OP_ALWAYS;
    depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
    depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

    RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
    RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
    dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = 2;
    dynamic_state_create_info.pDynamicStates    = dynamic_states;

    RHIGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shader_stages;
    pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
    pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
    pipelineInfo.pViewportState      = &viewport_state_create_info;
    pipelineInfo.pRasterizationState = &rasterization_state_create_info;
    pipelineInfo.pMultisampleState   = &multisample_state_create_info;
    pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
    pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
    pipelineInfo.layout              = m_render_pipelines[_render_pipeline_type_deferred_lighting].layout;
    pipelineInfo.renderPass          = m_framebuffer.render_pass;
    pipelineInfo.subpass             = _main_camera_subpass_deferred_lighting;
    pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
    pipelineInfo.pDynamicState       = &dynamic_state_create_info;

    if (RHI_SUCCESS != m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
        1,
        &pipelineInfo,
        m_render_pipelines[_render_pipeline_type_deferred_lighting].pipeline))
        throw std::runtime_error("create deferred lighting graphics pipeline");

    m_rhi->destroyShaderModule(vert_shader_module);
    m_rhi->destroyShaderModule(frag_shader_module);
}

// forward lighting
void MainCameraPass::setupForwardLightingPipeline() {
    RHIDescriptorSetLayout *descriptorset_layouts[3] = {m_descriptor_infos[_mesh_global].layout,
                                                        m_descriptor_infos[_per_mesh].layout,
                                                        m_descriptor_infos[_mesh_per_material].layout};
    RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 3;
    pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

    if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[_render_pipeline_type_forward_lighting].layout) != RHI_SUCCESS)
        throw std::runtime_error("create forward lighting pipeline layout");

    RHIShader* vert_shader_module = m_rhi->createShaderModule(MESH_VERT);
    RHIShader* frag_shader_module = m_rhi->createShaderModule(MESH_FRAG);

    RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
    vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
    vert_pipeline_shader_stage_create_info.module = vert_shader_module;
    vert_pipeline_shader_stage_create_info.pName  = "main";
    // vert_pipeline_shader_stage_create_info.pSpecializationInfo

    RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
    frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
    frag_pipeline_shader_stage_create_info.module = frag_shader_module;
    frag_pipeline_shader_stage_create_info.pName  = "main";

    RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                        frag_pipeline_shader_stage_create_info};

    auto vertex_binding_descriptions   = MeshVertex::getBindingDescriptions();
    auto vertex_attribute_descriptions = MeshVertex::getAttributeDescriptions();
    RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
    vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_create_info.vertexBindingDescriptionCount   = vertex_binding_descriptions.size();
    vertex_input_state_create_info.pVertexBindingDescriptions      = &vertex_binding_descriptions[0];
    vertex_input_state_create_info.vertexAttributeDescriptionCount = vertex_attribute_descriptions.size();
    vertex_input_state_create_info.pVertexAttributeDescriptions    = &vertex_attribute_descriptions[0];

    RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
    input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

    RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
    viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

    RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
    rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
    rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth               = 1.0f;
    rasterization_state_create_info.cullMode                = RHI_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp          = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

    RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
    multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
    multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

    std::vector<RHIPipelineColorBlendAttachmentState> color_blend_attachments(1);
    color_blend_attachments[0].colorWriteMask      = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                     RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
    color_blend_attachments[0].blendEnable         = RHI_FALSE;
    color_blend_attachments[0].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].colorBlendOp        = RHI_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].alphaBlendOp        = RHI_BLEND_OP_ADD;

    RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
    color_blend_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_create_info.logicOpEnable     = RHI_FALSE;
    color_blend_state_create_info.logicOp           = RHI_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount   = color_blend_attachments.size();
    color_blend_state_create_info.pAttachments      = color_blend_attachments.data();
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
    depth_stencil_create_info.sType                 = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_create_info.depthTestEnable       = RHI_TRUE;
    depth_stencil_create_info.depthWriteEnable      = RHI_TRUE;
    depth_stencil_create_info.depthCompareOp        = RHI_COMPARE_OP_LESS;
    depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
    depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

    RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
    RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
    dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = 2;
    dynamic_state_create_info.pDynamicStates    = dynamic_states;

    RHIGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shader_stages;
    pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
    pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
    pipelineInfo.pViewportState      = &viewport_state_create_info;
    pipelineInfo.pRasterizationState = &rasterization_state_create_info;
    pipelineInfo.pMultisampleState   = &multisample_state_create_info;
    pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
    pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
    pipelineInfo.layout              = m_render_pipelines[_render_pipeline_type_forward_lighting].layout;
    pipelineInfo.renderPass          = m_framebuffer.render_pass;
    pipelineInfo.subpass             = _main_camera_subpass_forward_lighting;
    pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
    pipelineInfo.pDynamicState       = &dynamic_state_create_info;

    if (m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
                                        1,
                                        &pipelineInfo,
                                        m_render_pipelines[_render_pipeline_type_forward_lighting].pipeline) !=
        RHI_SUCCESS)
        throw std::runtime_error("create forward lighting graphics pipeline");

    m_rhi->destroyShaderModule(vert_shader_module);
    m_rhi->destroyShaderModule(frag_shader_module);
}

// skybox
void MainCameraPass::setupSkyboxPipeline() {
    RHIDescriptorSetLayout *descriptorset_layouts[1] = {m_descriptor_infos[_skybox].layout};
    RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

    if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[_render_pipeline_type_skybox].layout) != RHI_SUCCESS)
        throw std::runtime_error("create skybox pipeline layout");

    RHIShader* vert_shader_module = m_rhi->createShaderModule(SKYBOX_VERT);
    RHIShader* frag_shader_module = m_rhi->createShaderModule(SKYBOX_FRAG);

    RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
    vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
    vert_pipeline_shader_stage_create_info.module = vert_shader_module;
    vert_pipeline_shader_stage_create_info.pName  = "main";
    // vert_pipeline_shader_stage_create_info.pSpecializationInfo

    RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
    frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
    frag_pipeline_shader_stage_create_info.module = frag_shader_module;
    frag_pipeline_shader_stage_create_info.pName  = "main";

    RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                        frag_pipeline_shader_stage_create_info};

    auto vertex_binding_descriptions   = MeshVertex::getBindingDescriptions();
    auto vertex_attribute_descriptions = MeshVertex::getAttributeDescriptions();
    RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
    vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_create_info.vertexBindingDescriptionCount   = 0;
    vertex_input_state_create_info.pVertexBindingDescriptions      = NULL;
    vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
    vertex_input_state_create_info.pVertexAttributeDescriptions    = NULL;

    RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
    input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

    RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
    viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

    RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
    rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
    rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth               = 1.0f;
    rasterization_state_create_info.cullMode                = RHI_CULL_MODE_BACK_BIT;
    rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp          = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

    RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
    multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
    multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

    std::vector<RHIPipelineColorBlendAttachmentState> color_blend_attachments(1);
    color_blend_attachments[0].colorWriteMask      = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                     RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
    color_blend_attachments[0].blendEnable         = RHI_FALSE;
    color_blend_attachments[0].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstColorBlendFactor = RHI_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].colorBlendOp        = RHI_BLEND_OP_ADD;
    color_blend_attachments[0].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachments[0].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ZERO;
    color_blend_attachments[0].alphaBlendOp        = RHI_BLEND_OP_ADD;

    RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info = {};
    color_blend_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_create_info.logicOpEnable     = RHI_FALSE;
    color_blend_state_create_info.logicOp           = RHI_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount   = color_blend_attachments.size();
    color_blend_state_create_info.pAttachments      = color_blend_attachments.data();
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
    depth_stencil_create_info.sType                 = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_create_info.depthTestEnable       = RHI_TRUE;
    depth_stencil_create_info.depthWriteEnable      = RHI_TRUE;
    depth_stencil_create_info.depthCompareOp        = RHI_COMPARE_OP_LESS;
    depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
    depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

    RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
    RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
    dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = 2;
    dynamic_state_create_info.pDynamicStates    = dynamic_states;

    RHIGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shader_stages;
    pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
    pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
    pipelineInfo.pViewportState      = &viewport_state_create_info;
    pipelineInfo.pRasterizationState = &rasterization_state_create_info;
    pipelineInfo.pMultisampleState   = &multisample_state_create_info;
    pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
    pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
    pipelineInfo.layout              = m_render_pipelines[_render_pipeline_type_skybox].layout;
    pipelineInfo.renderPass          = m_framebuffer.render_pass;
    pipelineInfo.subpass             = _main_camera_subpass_forward_lighting;
    pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
    pipelineInfo.pDynamicState       = &dynamic_state_create_info;

    if (RHI_SUCCESS != m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
        1,
        &pipelineInfo,
        m_render_pipelines[_render_pipeline_type_skybox].pipeline))
        throw std::runtime_error("create skybox graphics pipeline");

    m_rhi->destroyShaderModule(vert_shader_module);
    m_rhi->destroyShaderModule(frag_shader_module);
}

// draw axis
void MainCameraPass::setupAxisPipeline() {
    RHIDescriptorSetLayout *descriptorset_layouts[1] = {m_descriptor_infos[_axis].layout};
    RHIPipelineLayoutCreateInfo pipeline_layout_create_info {};
    pipeline_layout_create_info.sType          = RHI_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.setLayoutCount = 1;
    pipeline_layout_create_info.pSetLayouts    = descriptorset_layouts;

    if (m_rhi->createPipelineLayout(&pipeline_layout_create_info, m_render_pipelines[_render_pipeline_type_axis].layout) != RHI_SUCCESS)
        throw std::runtime_error("create axis pipeline layout");

    RHIShader* vert_shader_module = m_rhi->createShaderModule(AXIS_VERT);
    RHIShader* frag_shader_module = m_rhi->createShaderModule(AXIS_FRAG);

    RHIPipelineShaderStageCreateInfo vert_pipeline_shader_stage_create_info {};
    vert_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_VERTEX_BIT;
    vert_pipeline_shader_stage_create_info.module = vert_shader_module;
    vert_pipeline_shader_stage_create_info.pName  = "main";
    // vert_pipeline_shader_stage_create_info.pSpecializationInfo

    RHIPipelineShaderStageCreateInfo frag_pipeline_shader_stage_create_info {};
    frag_pipeline_shader_stage_create_info.sType  = RHI_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_pipeline_shader_stage_create_info.stage  = RHI_SHADER_STAGE_FRAGMENT_BIT;
    frag_pipeline_shader_stage_create_info.module = frag_shader_module;
    frag_pipeline_shader_stage_create_info.pName  = "main";

    RHIPipelineShaderStageCreateInfo shader_stages[] = {vert_pipeline_shader_stage_create_info,
                                                        frag_pipeline_shader_stage_create_info};

    auto vertex_binding_descriptions   = MeshVertex::getBindingDescriptions();
    auto vertex_attribute_descriptions = MeshVertex::getAttributeDescriptions();
    RHIPipelineVertexInputStateCreateInfo vertex_input_state_create_info {};
    vertex_input_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_state_create_info.vertexBindingDescriptionCount   = vertex_binding_descriptions.size();
    vertex_input_state_create_info.pVertexBindingDescriptions      = &vertex_binding_descriptions[0];
    vertex_input_state_create_info.vertexAttributeDescriptionCount = vertex_attribute_descriptions.size();
    vertex_input_state_create_info.pVertexAttributeDescriptions    = &vertex_attribute_descriptions[0];

    RHIPipelineInputAssemblyStateCreateInfo input_assembly_create_info {};
    input_assembly_create_info.sType    = RHI_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_create_info.topology = RHI_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly_create_info.primitiveRestartEnable = RHI_FALSE;

    RHIPipelineViewportStateCreateInfo viewport_state_create_info {};
    viewport_state_create_info.sType         = RHI_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state_create_info.viewportCount = 1;
    viewport_state_create_info.pViewports    = m_rhi->getSwapchainInfo().viewport;
    viewport_state_create_info.scissorCount  = 1;
    viewport_state_create_info.pScissors     = m_rhi->getSwapchainInfo().scissor;

    RHIPipelineRasterizationStateCreateInfo rasterization_state_create_info {};
    rasterization_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization_state_create_info.depthClampEnable        = RHI_FALSE;
    rasterization_state_create_info.rasterizerDiscardEnable = RHI_FALSE;
    rasterization_state_create_info.polygonMode             = RHI_POLYGON_MODE_FILL;
    rasterization_state_create_info.lineWidth               = 1.0f;
    rasterization_state_create_info.cullMode                = RHI_CULL_MODE_NONE;
    rasterization_state_create_info.frontFace               = RHI_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization_state_create_info.depthBiasEnable         = RHI_FALSE;
    rasterization_state_create_info.depthBiasConstantFactor = 0.0f;
    rasterization_state_create_info.depthBiasClamp          = 0.0f;
    rasterization_state_create_info.depthBiasSlopeFactor    = 0.0f;

    RHIPipelineMultisampleStateCreateInfo multisample_state_create_info {};
    multisample_state_create_info.sType = RHI_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state_create_info.sampleShadingEnable  = RHI_FALSE;
    multisample_state_create_info.rasterizationSamples = RHI_SAMPLE_COUNT_1_BIT;

    std::vector<RHIPipelineColorBlendAttachmentState> color_blend_attachment_state(1);
    color_blend_attachment_state[0].colorWriteMask      = RHI_COLOR_COMPONENT_R_BIT | RHI_COLOR_COMPONENT_G_BIT |
                                                          RHI_COLOR_COMPONENT_B_BIT | RHI_COLOR_COMPONENT_A_BIT;
    color_blend_attachment_state[0].blendEnable         = RHI_FALSE;
    color_blend_attachment_state[0].srcColorBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachment_state[0].dstColorBlendFactor = RHI_BLEND_FACTOR_ZERO;
    color_blend_attachment_state[0].colorBlendOp        = RHI_BLEND_OP_ADD;
    color_blend_attachment_state[0].srcAlphaBlendFactor = RHI_BLEND_FACTOR_ONE;
    color_blend_attachment_state[0].dstAlphaBlendFactor = RHI_BLEND_FACTOR_ZERO;
    color_blend_attachment_state[0].alphaBlendOp        = RHI_BLEND_OP_ADD;

    RHIPipelineColorBlendStateCreateInfo color_blend_state_create_info {};
    color_blend_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state_create_info.logicOpEnable     = RHI_FALSE;
    color_blend_state_create_info.logicOp           = RHI_LOGIC_OP_COPY;
    color_blend_state_create_info.attachmentCount   = color_blend_attachment_state.size();
    color_blend_state_create_info.pAttachments      = color_blend_attachment_state.data();
    color_blend_state_create_info.blendConstants[0] = 0.0f;
    color_blend_state_create_info.blendConstants[1] = 0.0f;
    color_blend_state_create_info.blendConstants[2] = 0.0f;
    color_blend_state_create_info.blendConstants[3] = 0.0f;

    RHIPipelineDepthStencilStateCreateInfo depth_stencil_create_info {};
    depth_stencil_create_info.sType                 = RHI_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil_create_info.depthTestEnable       = RHI_FALSE;
    depth_stencil_create_info.depthWriteEnable      = RHI_FALSE;
    depth_stencil_create_info.depthCompareOp        = RHI_COMPARE_OP_LESS;
    depth_stencil_create_info.depthBoundsTestEnable = RHI_FALSE;
    depth_stencil_create_info.stencilTestEnable     = RHI_FALSE;

    RHIDynamicState dynamic_states[] = {RHI_DYNAMIC_STATE_VIEWPORT, RHI_DYNAMIC_STATE_SCISSOR};
    RHIPipelineDynamicStateCreateInfo dynamic_state_create_info {};
    dynamic_state_create_info.sType             = RHI_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_create_info.dynamicStateCount = 2;
    dynamic_state_create_info.pDynamicStates    = dynamic_states;

    RHIGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType               = RHI_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shader_stages;
    pipelineInfo.pVertexInputState   = &vertex_input_state_create_info;
    pipelineInfo.pInputAssemblyState = &input_assembly_create_info;
    pipelineInfo.pViewportState      = &viewport_state_create_info;
    pipelineInfo.pRasterizationState = &rasterization_state_create_info;
    pipelineInfo.pMultisampleState   = &multisample_state_create_info;
    pipelineInfo.pColorBlendState    = &color_blend_state_create_info;
    pipelineInfo.pDepthStencilState  = &depth_stencil_create_info;
    pipelineInfo.layout              = m_render_pipelines[_render_pipeline_type_axis].layout;
    pipelineInfo.renderPass          = m_framebuffer.render_pass;
    pipelineInfo.subpass             = _main_camera_subpass_ui;
    pipelineInfo.basePipelineHandle  = RHI_NULL_HANDLE;
    pipelineInfo.pDynamicState       = &dynamic_state_create_info;

    if (RHI_SUCCESS != m_rhi->createGraphicsPipelines(RHI_NULL_HANDLE,
        1,
        &pipelineInfo,
        m_render_pipelines[_render_pipeline_type_axis].pipeline))
        throw std::runtime_error("create axis graphics pipeline");

    m_rhi->destroyShaderModule(vert_shader_module);
    m_rhi->destroyShaderModule(frag_shader_module);
}

void MainCameraPass::setupDescriptorSets() {
    setupMeshGlobalDescriptorSet();
    setupSkyboxDescriptorSet();
    setupAxisDescriptorSet();
    setupGbufferLightingDescriptorSet();
}

// update common model's global descriptor set
void MainCameraPass::setupMeshGlobalDescriptorSet() {
    RHIDescriptorSetAllocateInfo mesh_global_descriptor_set_alloc_info;
    mesh_global_descriptor_set_alloc_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    mesh_global_descriptor_set_alloc_info.pNext              = NULL;
    mesh_global_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPool();
    mesh_global_descriptor_set_alloc_info.descriptorSetCount = 1;
    mesh_global_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[_mesh_global].layout;

    if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&mesh_global_descriptor_set_alloc_info, m_descriptor_infos[_mesh_global].descriptor_set))
        throw std::runtime_error("allocate mesh global descriptor set");

    RHIDescriptorBufferInfo mesh_perframe_storage_buffer_info = {};
    mesh_perframe_storage_buffer_info.offset = 0; // this offset plus dynamic_offset should not be greater than the size of the buffer
    mesh_perframe_storage_buffer_info.range  = sizeof(MeshPerframeStorageBufferObject); // the range means the size actually used by the shader per draw call
    mesh_perframe_storage_buffer_info.buffer = m_global_render_resource->_storage_buffer._global_upload_ringbuffer;
    assert(mesh_perframe_storage_buffer_info.range < m_global_render_resource->_storage_buffer._max_storage_buffer_range);

    RHIDescriptorBufferInfo mesh_perdrawcall_storage_buffer_info = {};
    mesh_perdrawcall_storage_buffer_info.offset = 0;
    mesh_perdrawcall_storage_buffer_info.range  = sizeof(MeshPerdrawcallStorageBufferObject);
    mesh_perdrawcall_storage_buffer_info.buffer = m_global_render_resource->_storage_buffer._global_upload_ringbuffer;
    assert(mesh_perdrawcall_storage_buffer_info.range < m_global_render_resource->_storage_buffer._max_storage_buffer_range);

    RHIDescriptorBufferInfo mesh_per_drawcall_vertex_blending_storage_buffer_info = {};
    mesh_per_drawcall_vertex_blending_storage_buffer_info.offset = 0;
    mesh_per_drawcall_vertex_blending_storage_buffer_info.range  = sizeof(MeshPerdrawcallVertexBlendingStorageBufferObject);
    mesh_per_drawcall_vertex_blending_storage_buffer_info.buffer = m_global_render_resource->_storage_buffer._global_upload_ringbuffer;
    assert(mesh_per_drawcall_vertex_blending_storage_buffer_info.range < m_global_render_resource->_storage_buffer._max_storage_buffer_range);

    RHIDescriptorImageInfo brdf_texture_image_info = {};
    brdf_texture_image_info.sampler     = m_global_render_resource->_ibl_resource._brdfLUT_texture_sampler;
    brdf_texture_image_info.imageView   = m_global_render_resource->_ibl_resource._brdfLUT_texture_image_view;
    brdf_texture_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo irradiance_texture_image_info = {};
    irradiance_texture_image_info.sampler     = m_global_render_resource->_ibl_resource._irradiance_texture_sampler;
    irradiance_texture_image_info.imageView   = m_global_render_resource->_ibl_resource._irradiance_texture_image_view;
    irradiance_texture_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo specular_texture_image_info {};
    specular_texture_image_info.sampler     = m_global_render_resource->_ibl_resource._specular_texture_sampler;
    specular_texture_image_info.imageView   = m_global_render_resource->_ibl_resource._specular_texture_image_view;
    specular_texture_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo point_light_shadow_texture_image_info{};
    point_light_shadow_texture_image_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
    point_light_shadow_texture_image_info.imageView   = m_point_light_shadow_color_image_view;
    point_light_shadow_texture_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo directional_light_shadow_texture_image_info{};
    directional_light_shadow_texture_image_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
    directional_light_shadow_texture_image_info.imageView   = m_directional_light_shadow_color_image_view;
    directional_light_shadow_texture_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<RHIWriteDescriptorSet> mesh_descriptor_writes_info(8);
    mesh_descriptor_writes_info[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    mesh_descriptor_writes_info[0].pNext           = NULL;
    mesh_descriptor_writes_info[0].dstSet          = m_descriptor_infos[_mesh_global].descriptor_set;
    mesh_descriptor_writes_info[0].dstBinding      = 0;
    mesh_descriptor_writes_info[0].dstArrayElement = 0;
    mesh_descriptor_writes_info[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    mesh_descriptor_writes_info[0].descriptorCount = 1;
    mesh_descriptor_writes_info[0].pBufferInfo     = &mesh_perframe_storage_buffer_info;

    mesh_descriptor_writes_info[1]             = mesh_descriptor_writes_info[0];
    mesh_descriptor_writes_info[1].dstBinding  = 1;
    mesh_descriptor_writes_info[1].pBufferInfo = &mesh_perdrawcall_storage_buffer_info;

    mesh_descriptor_writes_info[2]             = mesh_descriptor_writes_info[0];
    mesh_descriptor_writes_info[2].dstBinding  = 2;
    mesh_descriptor_writes_info[2].pBufferInfo = &mesh_per_drawcall_vertex_blending_storage_buffer_info;

    mesh_descriptor_writes_info[3]                = mesh_descriptor_writes_info[0];
    mesh_descriptor_writes_info[3].dstBinding     = 3;
    mesh_descriptor_writes_info[3].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    mesh_descriptor_writes_info[3].pImageInfo     = &brdf_texture_image_info;

    mesh_descriptor_writes_info[4]            = mesh_descriptor_writes_info[3];
    mesh_descriptor_writes_info[4].dstBinding = 4;
    mesh_descriptor_writes_info[4].pImageInfo = &irradiance_texture_image_info;

    mesh_descriptor_writes_info[5]            = mesh_descriptor_writes_info[3];
    mesh_descriptor_writes_info[5].dstBinding = 5;
    mesh_descriptor_writes_info[5].pImageInfo = &specular_texture_image_info;

    mesh_descriptor_writes_info[6]            = mesh_descriptor_writes_info[3];
    mesh_descriptor_writes_info[6].dstBinding = 6;
    mesh_descriptor_writes_info[6].pImageInfo = &point_light_shadow_texture_image_info;

    mesh_descriptor_writes_info[7]            = mesh_descriptor_writes_info[3];
    mesh_descriptor_writes_info[7].dstBinding = 7;
    mesh_descriptor_writes_info[7].pImageInfo = &directional_light_shadow_texture_image_info;

    m_rhi->updateDescriptorSets(mesh_descriptor_writes_info.size(),
                                mesh_descriptor_writes_info.data(),
                                0,
                                NULL);
}

// setup the skybox descriptor set
void MainCameraPass::setupSkyboxDescriptorSet() {
    RHIDescriptorSetAllocateInfo skybox_descriptor_set_alloc_info;
    skybox_descriptor_set_alloc_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skybox_descriptor_set_alloc_info.pNext              = NULL;
    skybox_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPool();
    skybox_descriptor_set_alloc_info.descriptorSetCount = 1;
    skybox_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[_skybox].layout;

    if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&skybox_descriptor_set_alloc_info, m_descriptor_infos[_skybox].descriptor_set))
        throw std::runtime_error("allocate skybox descriptor set");

    RHIDescriptorBufferInfo mesh_perframe_storage_buffer_info = {};
    mesh_perframe_storage_buffer_info.offset = 0;
    mesh_perframe_storage_buffer_info.range  = sizeof(MeshPerframeStorageBufferObject);
    mesh_perframe_storage_buffer_info.buffer = m_global_render_resource->_storage_buffer._global_upload_ringbuffer;
    assert(mesh_perframe_storage_buffer_info.range < m_global_render_resource->_storage_buffer._max_storage_buffer_range);

    RHIDescriptorImageInfo specular_texture_image_info = {};
    specular_texture_image_info.sampler     = m_global_render_resource->_ibl_resource._specular_texture_sampler;
    specular_texture_image_info.imageView   = m_global_render_resource->_ibl_resource._specular_texture_image_view;
    specular_texture_image_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<RHIWriteDescriptorSet> skybox_descriptor_writes_info(2);
    skybox_descriptor_writes_info[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    skybox_descriptor_writes_info[0].pNext           = NULL;
    skybox_descriptor_writes_info[0].dstSet          = m_descriptor_infos[_skybox].descriptor_set;
    skybox_descriptor_writes_info[0].dstBinding      = 0;
    skybox_descriptor_writes_info[0].dstArrayElement = 0;
    skybox_descriptor_writes_info[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    skybox_descriptor_writes_info[0].descriptorCount = 1;
    skybox_descriptor_writes_info[0].pBufferInfo     = &mesh_perframe_storage_buffer_info;

    skybox_descriptor_writes_info[1] = skybox_descriptor_writes_info[0];
    skybox_descriptor_writes_info[1].dstBinding     = 1;
    skybox_descriptor_writes_info[1].descriptorType = RHI_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    skybox_descriptor_writes_info[1].pImageInfo     = &specular_texture_image_info;

    m_rhi->updateDescriptorSets(skybox_descriptor_writes_info.size(), skybox_descriptor_writes_info.data(), 0, NULL);
}

// setup the axis descriptor set
void MainCameraPass::setupAxisDescriptorSet() {
    RHIDescriptorSetAllocateInfo axis_descriptor_set_alloc_info;
    axis_descriptor_set_alloc_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    axis_descriptor_set_alloc_info.pNext              = NULL;
    axis_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPool();
    axis_descriptor_set_alloc_info.descriptorSetCount = 1;
    axis_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[_axis].layout;

    if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&axis_descriptor_set_alloc_info, m_descriptor_infos[_axis].descriptor_set))
        throw std::runtime_error("allocate axis descriptor set");

    RHIDescriptorBufferInfo mesh_perframe_storage_buffer_info = {};
    mesh_perframe_storage_buffer_info.offset = 0;
    mesh_perframe_storage_buffer_info.range  = sizeof(MeshPerframeStorageBufferObject);
    mesh_perframe_storage_buffer_info.buffer = m_global_render_resource->_storage_buffer._global_upload_ringbuffer;
    assert(mesh_perframe_storage_buffer_info.range < m_global_render_resource->_storage_buffer._max_storage_buffer_range);

    RHIDescriptorBufferInfo axis_storage_buffer_info = {};
    axis_storage_buffer_info.offset = 0;
    axis_storage_buffer_info.range  = sizeof(AxisStorageBufferObject);
    axis_storage_buffer_info.buffer = m_global_render_resource->_storage_buffer._axis_inefficient_storage_buffer;

    std::vector<RHIWriteDescriptorSet> axis_descriptor_writes_info(2);
    axis_descriptor_writes_info[0].sType           = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    axis_descriptor_writes_info[0].pNext           = NULL;
    axis_descriptor_writes_info[0].dstSet          = m_descriptor_infos[_axis].descriptor_set;
    axis_descriptor_writes_info[0].dstBinding      = 0;
    axis_descriptor_writes_info[0].dstArrayElement = 0;
    axis_descriptor_writes_info[0].descriptorType  = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    axis_descriptor_writes_info[0].descriptorCount = 1;
    axis_descriptor_writes_info[0].pBufferInfo     = &mesh_perframe_storage_buffer_info;

    axis_descriptor_writes_info[1]                = axis_descriptor_writes_info[0];
    axis_descriptor_writes_info[1].dstBinding     = 1;
    axis_descriptor_writes_info[1].descriptorType = RHI_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    axis_descriptor_writes_info[1].pBufferInfo    = &axis_storage_buffer_info;

    m_rhi->updateDescriptorSets(axis_descriptor_writes_info.size(),
                                axis_descriptor_writes_info.data(),
                                0,
                                NULL);
}

// setup the gbuffer lighting descriptor set
void MainCameraPass::setupGbufferLightingDescriptorSet() {
    RHIDescriptorSetAllocateInfo gbuffer_light_global_descriptor_set_alloc_info;
    gbuffer_light_global_descriptor_set_alloc_info.sType              = RHI_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    gbuffer_light_global_descriptor_set_alloc_info.pNext              = NULL;
    gbuffer_light_global_descriptor_set_alloc_info.descriptorPool     = m_rhi->getDescriptorPool();
    gbuffer_light_global_descriptor_set_alloc_info.descriptorSetCount = 1;
    gbuffer_light_global_descriptor_set_alloc_info.pSetLayouts        = &m_descriptor_infos[_deferred_lighting].layout;

    if (RHI_SUCCESS != m_rhi->allocateDescriptorSets(&gbuffer_light_global_descriptor_set_alloc_info, m_descriptor_infos[_deferred_lighting].descriptor_set))
        throw std::runtime_error("allocate gbuffer light global descriptor set");

    // 剩下的 RHIDescriptorBufferInfo, RHIDescriptorImageInfo 以及 RHIWriteDescriptorSet 的设置在 setupFramebufferDescriptorSet 中完成
    // 而且它们在窗口大小改变后 (updateAfterFramebufferRecreate) 需要被重新设置
    // 注意到 setupFramebufferDescriptorSet 是针对延迟渲染设计，前向渲染不需要 framebuffer 作为 descriptor set（不需要这些 input attachment）
}

// setup the framebuffer descriptor set
void MainCameraPass::setupFramebufferDescriptorSet() {
    RHIDescriptorImageInfo gbuffer_normal_input_attachment_info = {};
    gbuffer_normal_input_attachment_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
    gbuffer_normal_input_attachment_info.imageView   = m_framebuffer.attachments[_main_camera_pass_gbuffer_a].view;
    gbuffer_normal_input_attachment_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo gbuffer_metallic_roughness_shadingmodeid_input_attachment_info = {};
    gbuffer_metallic_roughness_shadingmodeid_input_attachment_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
    gbuffer_metallic_roughness_shadingmodeid_input_attachment_info.imageView   = m_framebuffer.attachments[_main_camera_pass_gbuffer_b].view;
    gbuffer_metallic_roughness_shadingmodeid_input_attachment_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo gbuffer_albedo_input_attachment_info = {};
    gbuffer_albedo_input_attachment_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
    gbuffer_albedo_input_attachment_info.imageView   = m_framebuffer.attachments[_main_camera_pass_gbuffer_c].view;
    gbuffer_albedo_input_attachment_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    RHIDescriptorImageInfo depth_input_attachment_info = {};
    depth_input_attachment_info.sampler     = m_rhi->getOrCreateDefaultSampler(Default_Sampler_Nearest);
    depth_input_attachment_info.imageView   = m_rhi->getDepthImageInfo().depth_image_view;
    depth_input_attachment_info.imageLayout = RHI_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::vector<RHIWriteDescriptorSet> deferred_lighting_descriptor_writes_info(4);
    // gbuffer_normal_descriptor_input_attachment_write_info
    deferred_lighting_descriptor_writes_info[0].sType = RHI_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    deferred_lighting_descriptor_writes_info[0].pNext = NULL;
    deferred_lighting_descriptor_writes_info[0].dstSet = m_descriptor_infos[_deferred_lighting].descriptor_set;
    deferred_lighting_descriptor_writes_info[0].dstBinding      = 0;
    deferred_lighting_descriptor_writes_info[0].dstArrayElement = 0;
    deferred_lighting_descriptor_writes_info[0].descriptorType  = RHI_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    deferred_lighting_descriptor_writes_info[0].descriptorCount = 1;
    deferred_lighting_descriptor_writes_info[0].pImageInfo      = &gbuffer_normal_input_attachment_info;

    // gbuffer_metallic_roughness_shadingmodeid_descriptor_input_attachment_write_info
    deferred_lighting_descriptor_writes_info[1]            = deferred_lighting_descriptor_writes_info[0];
    deferred_lighting_descriptor_writes_info[1].dstBinding = 1;
    deferred_lighting_descriptor_writes_info[1].pImageInfo = &gbuffer_metallic_roughness_shadingmodeid_input_attachment_info;

    // gbuffer_albedo_descriptor_input_attachment_write_info
    deferred_lighting_descriptor_writes_info[2]            = deferred_lighting_descriptor_writes_info[0];
    deferred_lighting_descriptor_writes_info[2].dstBinding = 2;
    deferred_lighting_descriptor_writes_info[2].pImageInfo = &gbuffer_albedo_input_attachment_info;

    // gbuffer_depth_descriptor_input_attachment_write_info
    deferred_lighting_descriptor_writes_info[3]            = deferred_lighting_descriptor_writes_info[0];
    deferred_lighting_descriptor_writes_info[3].dstBinding = 3;
    deferred_lighting_descriptor_writes_info[3].pImageInfo = &depth_input_attachment_info;

    m_rhi->updateDescriptorSets(deferred_lighting_descriptor_writes_info.size(),
                                deferred_lighting_descriptor_writes_info.data(),
                                0,
                                NULL);
}

void MainCameraPass::setupSwapchainFramebuffers() {
    m_swapchain_framebuffers.resize(m_rhi->getSwapchainInfo().imageViews.size());

    // create frame buffer for every imageview
    for (size_t i = 0; i < m_rhi->getSwapchainInfo().imageViews.size(); i++) {
        RHIImageView* framebuffer_attachments_for_image_view[_main_camera_pass_attachment_count] = {
            m_framebuffer.attachments[_main_camera_pass_gbuffer_a].view,
            m_framebuffer.attachments[_main_camera_pass_gbuffer_b].view,
            m_framebuffer.attachments[_main_camera_pass_gbuffer_c].view,
            m_framebuffer.attachments[_main_camera_pass_backup_buffer_a].view,
            m_framebuffer.attachments[_main_camera_pass_backup_buffer_b].view,
            m_framebuffer.attachments[_main_camera_pass_post_process_buffer_odd].view,
            m_framebuffer.attachments[_main_camera_pass_post_process_buffer_even].view,
            m_rhi->getDepthImageInfo().depth_image_view,
            m_rhi->getSwapchainInfo().imageViews[i]
        };

        RHIFramebufferCreateInfo framebuffer_create_info {};
        framebuffer_create_info.sType           = RHI_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.flags           = 0U;
        framebuffer_create_info.renderPass      = m_framebuffer.render_pass;
        framebuffer_create_info.attachmentCount = _main_camera_pass_attachment_count;
        framebuffer_create_info.pAttachments    = framebuffer_attachments_for_image_view;
        framebuffer_create_info.width           = m_rhi->getSwapchainInfo().extent.width;
        framebuffer_create_info.height          = m_rhi->getSwapchainInfo().extent.height;
        framebuffer_create_info.layers          = 1;

        m_swapchain_framebuffers[i] = new VulkanFramebuffer();
        if (RHI_SUCCESS != m_rhi->createFramebuffer(&framebuffer_create_info, m_swapchain_framebuffers[i]))
            throw std::runtime_error("create main camera framebuffer");
    }
}

void MainCameraPass::updateAfterFramebufferRecreate() {
    for (size_t i = 0; i < m_framebuffer.attachments.size(); i++) {
        m_rhi->destroyImage(m_framebuffer.attachments[i].image);
        m_rhi->destroyImageView(m_framebuffer.attachments[i].view);
        m_rhi->freeMemory(m_framebuffer.attachments[i].mem);
    }

    for (auto framebuffer : m_swapchain_framebuffers) {
        m_rhi->destroyFramebuffer(framebuffer);
        delete framebuffer;
    }
    m_swapchain_framebuffers.clear();

    setupAttachments();

    setupFramebufferDescriptorSet();

    setupSwapchainFramebuffers();

    setupParticlePass();
}

// main camera pass draw function for deferred rendering
void MainCameraPass::draw(ColorGradingPass &color_grading_pass,
                          VignettePass     &vignette_pass,
                          FXAAPass         &fxaa_pass,
                          ToneMappingPass  &tone_mapping_pass,
                          UIPass           &ui_pass,
                          CombineUIPass    &combine_ui_pass,
                          ParticlePass     &particle_pass,
                          uint32_t          current_swapchain_image_index) {
    {
        RHIRenderPassBeginInfo renderpass_begin_info {};
        renderpass_begin_info.sType             = RHI_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderpass_begin_info.renderPass        = m_framebuffer.render_pass;
        renderpass_begin_info.framebuffer       = m_swapchain_framebuffers[current_swapchain_image_index];
        renderpass_begin_info.renderArea.offset = {0, 0};
        renderpass_begin_info.renderArea.extent = m_rhi->getSwapchainInfo().extent;

        RHIClearValue clear_values[_main_camera_pass_attachment_count];
        clear_values[_main_camera_pass_gbuffer_a].color                = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[_main_camera_pass_gbuffer_b].color                = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[_main_camera_pass_gbuffer_c].color                = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[_main_camera_pass_backup_buffer_a].color        = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clear_values[_main_camera_pass_backup_buffer_b].color       = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clear_values[_main_camera_pass_post_process_buffer_odd].color  = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clear_values[_main_camera_pass_post_process_buffer_even].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clear_values[_main_camera_pass_depth].depthStencil             = {1.0f, 0};
        clear_values[_main_camera_pass_swap_chain_image].color         = {{0.0f, 0.0f, 0.0f, 1.0f}};
        renderpass_begin_info.clearValueCount = (sizeof(clear_values) / sizeof(clear_values[0]));
        renderpass_begin_info.pClearValues    = clear_values;

        m_rhi->cmdBeginRenderPassPFN(m_rhi->getCurrentCommandBuffer(), &renderpass_begin_info, RHI_SUBPASS_CONTENTS_INLINE);
    }

    // ----- base pass -----
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "BasePass of G-Buffer", color);
    drawMesh(_render_pipeline_type_mesh_gbuffer); // 延迟渲染首先绘制 GBuffer
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- deferred lighting pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Deferred Lighting", color);
    drawDeferredLighting();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- forward lighting pass -----
    m_rhi->pushEvent(particle_pass.getRenderCommandBufferHandle(), "Forward Lighting of ParticleBillboard", color);
    particle_pass.draw();
    m_rhi->popEvent(particle_pass.getRenderCommandBufferHandle());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- tone mapping pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Tone Map", color);
    tone_mapping_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- color grading pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Color Grading", color);
    color_grading_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- vignette pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Vignette", color);
    vignette_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- FXAA pass (optionally jumped) -----
    if (m_enable_fxaa) {
        m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "FXAA", color);
        fxaa_pass.draw();
        m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    }
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- UI pass -----
    RHIClearAttachment clear_attachments[1];
    clear_attachments[0].aspectMask                  = RHI_IMAGE_ASPECT_COLOR_BIT;
    clear_attachments[0].colorAttachment             = 0;
    clear_attachments[0].clearValue.color.float32[0] = 0.0;
    clear_attachments[0].clearValue.color.float32[1] = 0.0;
    clear_attachments[0].clearValue.color.float32[2] = 0.0;
    clear_attachments[0].clearValue.color.float32[3] = 0.0;
    RHIClearRect clear_rects[1];
    clear_rects[0].baseArrayLayer     = 0;
    clear_rects[0].layerCount         = 1;
    clear_rects[0].rect.offset.x      = 0;
    clear_rects[0].rect.offset.y      = 0;
    clear_rects[0].rect.extent.width  = m_rhi->getSwapchainInfo().extent.width;
    clear_rects[0].rect.extent.height = m_rhi->getSwapchainInfo().extent.height;
    m_rhi->cmdClearAttachmentsPFN(m_rhi->getCurrentCommandBuffer(),
                                  sizeof(clear_attachments) / sizeof(clear_attachments[0]),
                                  clear_attachments,
                                  sizeof(clear_rects) / sizeof(clear_rects[0]),
                                  clear_rects);
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "UI of Axis", color);
    drawAxis();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "UI of ImGUI", color);
    ui_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());

    // ----- combine UI pass -----
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Combine UI", color);
    combine_ui_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());

    m_rhi->cmdEndRenderPassPFN(m_rhi->getCurrentCommandBuffer());
}

// main camera pass draw function for forward rendering
void MainCameraPass::drawForward(ColorGradingPass &color_grading_pass,
                                 VignettePass     &vignette_pass,
                                 FXAAPass         &fxaa_pass,
                                 ToneMappingPass  &tone_mapping_pass,
                                 UIPass           &ui_pass,
                                 CombineUIPass    &combine_ui_pass,
                                 ParticlePass     &particle_pass,
                                 uint32_t          current_swapchain_image_index) {
    {
        RHIRenderPassBeginInfo renderpass_begin_info {};
        renderpass_begin_info.sType             = RHI_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderpass_begin_info.renderPass        = m_framebuffer.render_pass;
        renderpass_begin_info.framebuffer       = m_swapchain_framebuffers[current_swapchain_image_index];
        renderpass_begin_info.renderArea.offset = {0, 0};
        renderpass_begin_info.renderArea.extent = m_rhi->getSwapchainInfo().extent;

        RHIClearValue clear_values[_main_camera_pass_attachment_count];
        clear_values[_main_camera_pass_gbuffer_a].color          = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[_main_camera_pass_gbuffer_b].color          = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[_main_camera_pass_gbuffer_c].color          = {{0.0f, 0.0f, 0.0f, 0.0f}};
        clear_values[_main_camera_pass_backup_buffer_a].color  = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clear_values[_main_camera_pass_backup_buffer_b].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clear_values[_main_camera_pass_depth].depthStencil       = {1.0f, 0};
        clear_values[_main_camera_pass_swap_chain_image].color   = {{0.0f, 0.0f, 0.0f, 1.0f}};
        renderpass_begin_info.clearValueCount                    = (sizeof(clear_values) / sizeof(clear_values[0]));
        renderpass_begin_info.pClearValues                       = clear_values;

        m_rhi->cmdBeginRenderPassPFN(m_rhi->getCurrentCommandBuffer(), &renderpass_begin_info, RHI_SUBPASS_CONTENTS_INLINE);
    }

    // ----- base pass (jumped) -----
    float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- deferred lighting pass (jumped) -----
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- forward lighting pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Forward Lighting of Model", color);
    drawMesh(_render_pipeline_type_forward_lighting);
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());

    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Forward Lighting of Skybox", color);
    drawSkybox();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());

    m_rhi->pushEvent(particle_pass.getRenderCommandBufferHandle(), "Forward Lighting of ParticleBillboard", color);
    particle_pass.draw();
    m_rhi->popEvent(particle_pass.getRenderCommandBufferHandle());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- tone mapping pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Tone Map", color);
    tone_mapping_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- color grading pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Color Grading", color);
    color_grading_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- vignette pass -----
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Vignette", color);
    vignette_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- FXAA pass (optionally jumped) -----
    if (m_enable_fxaa) {
        m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "FXAA", color);
        fxaa_pass.draw();
        m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    }
    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);

    // ----- UI pass -----
    RHIClearAttachment clear_attachments[1];
    clear_attachments[0].aspectMask                  = RHI_IMAGE_ASPECT_COLOR_BIT;
    clear_attachments[0].colorAttachment             = 0;
    clear_attachments[0].clearValue.color.float32[0] = 0.0;
    clear_attachments[0].clearValue.color.float32[1] = 0.0;
    clear_attachments[0].clearValue.color.float32[2] = 0.0;
    clear_attachments[0].clearValue.color.float32[3] = 0.0;
    RHIClearRect clear_rects[1];
    clear_rects[0].baseArrayLayer     = 0;
    clear_rects[0].layerCount         = 1;
    clear_rects[0].rect.offset.x      = 0;
    clear_rects[0].rect.offset.y      = 0;
    clear_rects[0].rect.extent.width  = m_rhi->getSwapchainInfo().extent.width;
    clear_rects[0].rect.extent.height = m_rhi->getSwapchainInfo().extent.height;
    m_rhi->cmdClearAttachmentsPFN(m_rhi->getCurrentCommandBuffer(),
                                  sizeof(clear_attachments) / sizeof(clear_attachments[0]),
                                  clear_attachments,
                                  sizeof(clear_rects) / sizeof(clear_rects[0]),
                                  clear_rects);
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "UI of Axis", color);
    drawAxis();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "UI of ImGUI", color);
    ui_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());

    m_rhi->cmdNextSubpassPFN(m_rhi->getCurrentCommandBuffer(), RHI_SUBPASS_CONTENTS_INLINE);
    m_rhi->pushEvent(m_rhi->getCurrentCommandBuffer(), "Combine UI", color);
    combine_ui_pass.draw();
    m_rhi->popEvent(m_rhi->getCurrentCommandBuffer());

    m_rhi->cmdEndRenderPassPFN(m_rhi->getCurrentCommandBuffer());
}

// 根据 material 和 mesh 进行重新分组
std::map<VulkanPBRMaterial*, std::map<VulkanMesh*, std::vector<MeshNode>>> MainCameraPass::reorganizeMeshNodes(std::vector<RenderMeshNode>* visible_mesh_nodes) {
    std::map<VulkanPBRMaterial*, std::map<VulkanMesh*, std::vector<MeshNode>>> drawcall_batch;

    for (const RenderMeshNode& node : *visible_mesh_nodes) {
        MeshNode mesh_node;
        mesh_node.model_matrix = node.model_matrix;
        if (node.enable_vertex_blending) {
            mesh_node.joint_matrices = node.joint_matrices;
            mesh_node.joint_count = node.joint_count;
        }
        auto& mesh_map = drawcall_batch[node.ref_material]; // [] 操作符，找不着就创建新的，找到了就用已有的
        mesh_map[node.ref_mesh].push_back(std::move(mesh_node));
    }

    return drawcall_batch;
}

template<typename T>
MainCameraPass::RingBufferAllocation<T> MainCameraPass::allocateRingBufferSpace() {
    uint32_t dynamic_offset = roundUp(
        m_global_render_resource->_storage_buffer._global_upload_ringbuffers_end[m_rhi->getCurrentFrameIndex()],
        m_global_render_resource->_storage_buffer._min_storage_buffer_offset_alignment);

    m_global_render_resource->_storage_buffer._global_upload_ringbuffers_end[m_rhi->getCurrentFrameIndex()] =
            dynamic_offset + sizeof(T);

    assert(m_global_render_resource->_storage_buffer._global_upload_ringbuffers_end[m_rhi->getCurrentFrameIndex()] <=
          (m_global_render_resource->_storage_buffer._global_upload_ringbuffers_begin[m_rhi->getCurrentFrameIndex()] +
           m_global_render_resource->_storage_buffer._global_upload_ringbuffers_size[m_rhi->getCurrentFrameIndex()]));

    T* data_ptr = reinterpret_cast<T*>(
        reinterpret_cast<uintptr_t>(m_global_render_resource->_storage_buffer._global_upload_ringbuffer_memory_pointer) +
        dynamic_offset);

    return {data_ptr, dynamic_offset};
}

void MainCameraPass::drawMesh(RenderPipeLineType render_pipeline_type) {
    // reorganize mesh
    auto drawcall_batch = reorganizeMeshNodes(m_visible_nodes.p_main_camera_visible_mesh_nodes);

    m_rhi->cmdBindPipelinePFN(m_rhi->getCurrentCommandBuffer(),
                              RHI_PIPELINE_BIND_POINT_GRAPHICS,
                              m_render_pipelines[render_pipeline_type].pipeline);
    m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().viewport);
    m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().scissor);

    // perframe storage buffer
    auto perframe_allocation = allocateRingBufferSpace<MeshPerframeStorageBufferObject>();
    *perframe_allocation.data_ptr = m_mesh_perframe_storage_buffer_object;
    uint32_t perframe_dynamic_offset = perframe_allocation.dynamic_offset;

    for (auto &pair1 : drawcall_batch) {
        VulkanPBRMaterial &material = (*pair1.first);
        auto &mesh_instanced = pair1.second;

        // bind per material
        m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                        RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_render_pipelines[render_pipeline_type].layout,
                                        2,
                                        1,
                                        &material.material_descriptor_set,
                                        0,
                                        NULL);

        // TODO: render from near to far

        for (auto &pair2 : mesh_instanced) {
            VulkanMesh &mesh = (*pair2.first);
            auto &mesh_nodes = pair2.second;

            uint32_t total_instance_count = static_cast<uint32_t>(mesh_nodes.size());
            if (total_instance_count > 0) {
                // bind per mesh
                m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                                RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_render_pipelines[render_pipeline_type].layout,
                                                1,
                                                1,
                                                &mesh.mesh_vertex_blending_descriptor_set,
                                                0,
                                                NULL);

                RHIBuffer* vertex_buffers[] = {mesh.mesh_vertex_position_buffer,
                                               mesh.mesh_vertex_varying_enable_blending_buffer,
                                               mesh.mesh_vertex_varying_buffer
                                              };
                RHIDeviceSize offsets[]        = {0, 0, 0};
                m_rhi->cmdBindVertexBuffersPFN(m_rhi->getCurrentCommandBuffer(),
                                               0,
                                               (sizeof(vertex_buffers) / sizeof(vertex_buffers[0])),
                                               vertex_buffers,
                                               offsets);
                m_rhi->cmdBindIndexBufferPFN(m_rhi->getCurrentCommandBuffer(), mesh.mesh_index_buffer, 0, RHI_INDEX_TYPE_UINT16);

                uint32_t drawcall_max_instance_count =
                    (sizeof(MeshPerdrawcallStorageBufferObject::mesh_instances) /
                     sizeof(MeshPerdrawcallStorageBufferObject::mesh_instances[0]));
                uint32_t drawcall_count =
                    roundUp(total_instance_count, drawcall_max_instance_count) / drawcall_max_instance_count;

                for (uint32_t drawcall_index = 0; drawcall_index < drawcall_count; ++drawcall_index) {
                    uint32_t current_instance_count =
                        ((total_instance_count - drawcall_max_instance_count * drawcall_index) <
                         drawcall_max_instance_count) ?
                        (total_instance_count - drawcall_max_instance_count * drawcall_index) :
                        drawcall_max_instance_count;

                    // per drawcall storage buffer
                    auto per_drawcall_allocation = allocateRingBufferSpace<MeshPerdrawcallStorageBufferObject>();
                    uint32_t per_drawcall_dynamic_offset = per_drawcall_allocation.dynamic_offset;
                    for (uint32_t i = 0; i < current_instance_count; ++i) {
                        per_drawcall_allocation.data_ptr->mesh_instances[i].model_matrix =
                            *mesh_nodes[drawcall_max_instance_count * drawcall_index + i].model_matrix;
                        per_drawcall_allocation.data_ptr->mesh_instances[i].enable_vertex_blending =
                            mesh_nodes[drawcall_max_instance_count * drawcall_index + i].joint_matrices ? 1.0f : -1.0f;
                    }

                    // per drawcall vertex blending storage buffer
                    uint32_t per_drawcall_vertex_blending_dynamic_offset;
                    bool     least_one_enable_vertex_blending = true;
                    for (uint32_t i = 0; i < current_instance_count; ++i) {
                        if (!mesh_nodes[drawcall_max_instance_count * drawcall_index + i].joint_matrices) {
                            least_one_enable_vertex_blending = false;
                            break;
                        }
                    }
                    if (least_one_enable_vertex_blending) {
                        auto per_drawcall_vertex_blending_allocation = allocateRingBufferSpace<MeshPerdrawcallVertexBlendingStorageBufferObject>();
                        per_drawcall_vertex_blending_dynamic_offset = per_drawcall_vertex_blending_allocation.dynamic_offset;
                        for (uint32_t i = 0; i < current_instance_count; ++i) {
                            if (mesh_nodes[drawcall_max_instance_count * drawcall_index + i].joint_matrices) {
                                for (uint32_t j = 0; j < mesh_nodes[drawcall_max_instance_count * drawcall_index + i].joint_count; ++j) {
                                    per_drawcall_vertex_blending_allocation.data_ptr->joint_matrices[s_mesh_vertex_blending_max_joint_count * i + j] =
                                        mesh_nodes[drawcall_max_instance_count * drawcall_index + i].joint_matrices[j];
                                }
                            }
                        }
                    } else {
                        per_drawcall_vertex_blending_dynamic_offset = 0;
                    }

                    // bind perdrawcall
                    uint32_t dynamic_offsets[3] = {perframe_dynamic_offset,
                                                   per_drawcall_dynamic_offset,
                                                   per_drawcall_vertex_blending_dynamic_offset};
                    m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                                    RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                                    m_render_pipelines[render_pipeline_type].layout,
                                                    0,
                                                    1,
                                                    &m_descriptor_infos[_mesh_global].descriptor_set,
                                                    3,
                                                    dynamic_offsets);

                    m_rhi->cmdDrawIndexedPFN(m_rhi->getCurrentCommandBuffer(),
                                             mesh.mesh_index_count,
                                             current_instance_count,
                                             0,
                                             0,
                                             0);
                }
            }
        }
    }
}

void MainCameraPass::drawDeferredLighting() {
    m_rhi->cmdBindPipelinePFN(m_rhi->getCurrentCommandBuffer(),
                              RHI_PIPELINE_BIND_POINT_GRAPHICS,
                              m_render_pipelines[_render_pipeline_type_deferred_lighting].pipeline);

    m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().viewport);
    m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().scissor);

    auto perframe_allocation = allocateRingBufferSpace<MeshPerframeStorageBufferObject>();
    *perframe_allocation.data_ptr = m_mesh_perframe_storage_buffer_object;
    uint32_t perframe_dynamic_offset = perframe_allocation.dynamic_offset;

    RHIDescriptorSet* descriptor_sets[3] = {m_descriptor_infos[_mesh_global].descriptor_set,
                                            m_descriptor_infos[_deferred_lighting].descriptor_set,
                                            m_descriptor_infos[_skybox].descriptor_set};
    uint32_t dynamic_offsets[4] = {perframe_dynamic_offset, perframe_dynamic_offset, 0, 0};
    m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                    RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_render_pipelines[_render_pipeline_type_deferred_lighting].layout,
                                    0,
                                    3,
                                    descriptor_sets,
                                    4,
                                    dynamic_offsets);

    m_rhi->cmdDraw(m_rhi->getCurrentCommandBuffer(), 3, 1, 0, 0);
}

// forward rendering 的天空盒绘制，延迟渲染的天空盒被整合在 drawDeferredLighting() 中
void MainCameraPass::drawSkybox() {
    auto perframe_allocation = allocateRingBufferSpace<MeshPerframeStorageBufferObject>();
    *perframe_allocation.data_ptr = m_mesh_perframe_storage_buffer_object;
    uint32_t perframe_dynamic_offset = perframe_allocation.dynamic_offset;

    m_rhi->cmdBindPipelinePFN(m_rhi->getCurrentCommandBuffer(),
                              RHI_PIPELINE_BIND_POINT_GRAPHICS,
                              m_render_pipelines[_render_pipeline_type_skybox].pipeline);
    m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                    RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_render_pipelines[_render_pipeline_type_skybox].layout,
                                    0,
                                    1,
                                    &m_descriptor_infos[_skybox].descriptor_set,
                                    1,
                                    &perframe_dynamic_offset);
    m_rhi->cmdDraw(m_rhi->getCurrentCommandBuffer(), 36, 1, 0, 0); // 2 triangles(6 vertex) each face, 6 faces
}

void MainCameraPass::drawAxis() {
    if (!m_is_show_axis)
        return;

    auto perframe_allocation = allocateRingBufferSpace<MeshPerframeStorageBufferObject>();
    *perframe_allocation.data_ptr = m_mesh_perframe_storage_buffer_object;
    uint32_t perframe_dynamic_offset = perframe_allocation.dynamic_offset;

    m_rhi->cmdBindPipelinePFN(m_rhi->getCurrentCommandBuffer(),
                              RHI_PIPELINE_BIND_POINT_GRAPHICS,
                              m_render_pipelines[_render_pipeline_type_axis].pipeline);
    m_rhi->cmdSetViewportPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().viewport);
    m_rhi->cmdSetScissorPFN(m_rhi->getCurrentCommandBuffer(), 0, 1, m_rhi->getSwapchainInfo().scissor);
    m_rhi->cmdBindDescriptorSetsPFN(m_rhi->getCurrentCommandBuffer(),
                                    RHI_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_render_pipelines[_render_pipeline_type_axis].layout,
                                    0,
                                    1,
                                    &m_descriptor_infos[_axis].descriptor_set,
                                    1,
                                    &perframe_dynamic_offset);

    m_axis_storage_buffer_object.selected_axis = m_selected_axis;
    m_axis_storage_buffer_object.model_matrix  = m_visible_nodes.p_axis_node->model_matrix;

    RHIBuffer* vertex_buffers[3] = {m_visible_nodes.p_axis_node->ref_mesh->mesh_vertex_position_buffer,
                                    m_visible_nodes.p_axis_node->ref_mesh->mesh_vertex_varying_enable_blending_buffer,
                                    m_visible_nodes.p_axis_node->ref_mesh->mesh_vertex_varying_buffer};
    RHIDeviceSize offsets[3] = {0, 0, 0};
    m_rhi->cmdBindVertexBuffersPFN(m_rhi->getCurrentCommandBuffer(),
                                   0,
                                   (sizeof(vertex_buffers) / sizeof(vertex_buffers[0])),
                                   vertex_buffers,
                                   offsets);
    m_rhi->cmdBindIndexBufferPFN(m_rhi->getCurrentCommandBuffer(),
                                 m_visible_nodes.p_axis_node->ref_mesh->mesh_index_buffer,
                                 0,
                                 RHI_INDEX_TYPE_UINT16);
    (*reinterpret_cast<AxisStorageBufferObject*>(reinterpret_cast<uintptr_t>(
            m_global_render_resource->_storage_buffer._axis_inefficient_storage_buffer_memory_pointer))) =
                m_axis_storage_buffer_object;

    m_rhi->cmdDrawIndexedPFN(m_rhi->getCurrentCommandBuffer(),
                             m_visible_nodes.p_axis_node->ref_mesh->mesh_index_count,
                             1,
                             0,
                             0,
                             0);
}

void MainCameraPass::setupParticlePass() {
    m_particle_pass->setDepthAndNormalImage(m_rhi->getDepthImageInfo().depth_image,
                                            m_framebuffer.attachments[_main_camera_pass_gbuffer_a].image);

    m_particle_pass->setRenderPassHandle(m_framebuffer.render_pass);
}

void MainCameraPass::setParticlePass(std::shared_ptr<ParticlePass> pass) { m_particle_pass = pass; }

} // namespace Piccolo
