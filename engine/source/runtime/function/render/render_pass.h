#pragma once

#include "runtime/function/render/render_common.h"
#include "runtime/function/render/render_pass_base.h"
#include "runtime/function/render/render_resource.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

namespace Piccolo {
class VulkanRHI;

// attachment of main camera render pass
enum {
    _main_camera_pass_gbuffer_a = 0,
    _main_camera_pass_gbuffer_b,
    _main_camera_pass_gbuffer_c,
    _main_camera_pass_backup_buffer_a,
    _main_camera_pass_backup_buffer_b,          // 目前还未用到，留空
    _main_camera_pass_post_process_buffer_odd,  // 用于后处理时左右手来回倒腾
    _main_camera_pass_post_process_buffer_even,
    _main_camera_pass_depth,
    _main_camera_pass_swap_chain_image,
    _main_camera_pass_attachment_count,                  // 最后三个 enum 用于计数
    _main_camera_pass_custom_attachment_count       = 5,
    _main_camera_pass_post_process_attachment_count = 2,
};

// subpass of main camera render pass
enum {
    _main_camera_subpass_basepass = 0,
    _main_camera_subpass_deferred_lighting,
    _main_camera_subpass_forward_lighting,
    _main_camera_subpass_tone_mapping,
    _main_camera_subpass_color_grading,
    _main_camera_subpass_vignette,
    _main_camera_subpass_fxaa,
    _main_camera_subpass_ui,
    _main_camera_subpass_combine_ui,
    _main_camera_subpass_count, // 最后一个 enum 用于计数
};

struct VisibleNodes {
    std::vector<RenderMeshNode>*              p_directional_light_visible_mesh_nodes {nullptr};
    std::vector<RenderMeshNode>*              p_point_lights_visible_mesh_nodes {nullptr};
    std::vector<RenderMeshNode>*              p_main_camera_visible_mesh_nodes {nullptr};
    RenderAxisNode*                           p_axis_node {nullptr};
};

class RenderPass : public RenderPassBase {
public:
    struct FrameBufferAttachment {
        RHIImage*        image;
        RHIDeviceMemory* mem;
        RHIImageView*    view;
        RHIFormat       format;
    };

    struct Framebuffer {
        int           width;
        int           height;
        RHIFramebuffer* framebuffer;
        RHIRenderPass*  render_pass;

        std::vector<FrameBufferAttachment> attachments;
    };

    struct Descriptor {
        RHIDescriptorSetLayout* layout;
        RHIDescriptorSet*       descriptor_set;
    };

    struct RenderPipelineBase {
        RHIPipelineLayout* layout;
        RHIPipeline*       pipeline;
    };

    GlobalRenderResource* m_global_render_resource {nullptr}; // 该 pass 可能使用的全局资源，如 IBL、color grading, storage buffer

    std::vector<Descriptor>         m_descriptor_infos; // 描述符信息
    std::vector<RenderPipelineBase> m_render_pipelines; // 渲染管线信息
    Framebuffer                     m_framebuffer;      // 帧缓冲信息

    void initialize(const RenderPassInitInfo* init_info) override;
    void postInitialize() override;

    virtual void draw();

    virtual RHIRenderPass*                       getRenderPass() const;
    virtual std::vector<RHIImageView*>           getFramebufferImageViews() const;
    virtual std::vector<RHIDescriptorSetLayout*> getDescriptorSetLayouts() const;

    static VisibleNodes m_visible_nodes; // 可见对象，在所有 render pass 中共享（所以是 static）

private:
};
} // namespace Piccolo
