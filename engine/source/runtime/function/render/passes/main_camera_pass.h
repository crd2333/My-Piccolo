#pragma once

#include "runtime/function/render/render_pass.h"
#include "runtime/function/render/render_resource.h"

#include "runtime/function/render/passes/color_grading_pass.h"
#include "runtime/function/render/passes/vignette_pass.h"
#include "runtime/function/render/passes/combine_ui_pass.h"
#include "runtime/function/render/passes/fxaa_pass.h"
#include "runtime/function/render/passes/tone_mapping_pass.h"
#include "runtime/function/render/passes/ui_pass.h"
#include "runtime/function/render/passes/particle_pass.h"

#include <map>

// passes 文件夹下一些是 render pass，一些是 subpass
// 像 MainCameraPass 就是一个很大的 render pass，包含了多个 subpass

namespace Piccolo {
class RenderResourceBase;

struct MainCameraPassInitInfo : RenderPassInitInfo {
    bool enable_fxaa;
};

// RenderMeshNode 中的 ref 信息、node_id、enable_vertex_blending 信息在合批后不需要
struct MeshNode {
    const Matrix4x4* model_matrix {nullptr};
    const Matrix4x4* joint_matrices {nullptr};
    uint32_t         joint_count {0};
};

class MainCameraPass : public RenderPass {
public:
    // 1: per mesh layout
    // 2: global layout
    // 3: mesh per material layout
    // 4: sky box layout
    // 5: axis layout
    // 6: billboard type particle layout
    // 7: gbuffer lighting
    enum LayoutType : uint8_t {
        _per_mesh = 0,
        _mesh_global,
        _mesh_per_material,
        _skybox,
        _axis,
        _particle,
        _deferred_lighting,
        _layout_type_count
    };

    // 1. model
    // 2. sky box
    // 3. axis
    // 4. billboard type particle
    enum RenderPipeLineType : uint8_t {
        _render_pipeline_type_mesh_gbuffer = 0,
        _render_pipeline_type_deferred_lighting,
        _render_pipeline_type_mesh_lighting,
        _render_pipeline_type_skybox,
        _render_pipeline_type_axis,
        _render_pipeline_type_particle,
        _render_pipeline_type_count
    };

    void initialize(const RenderPassInitInfo* init_info) override final;

    void preparePassData(std::shared_ptr<RenderResourceBase> render_resource) override final;

    void draw(ColorGradingPass &color_grading_pass,
              VignettePass &vignette_pass,
              FXAAPass &fxaa_pass,
              ToneMappingPass &tone_mapping_pass,
              UIPass &ui_pass,
              CombineUIPass &combine_ui_pass,
              ParticlePass &particle_pass,
              uint32_t current_swapchain_image_index);

    void drawForward(ColorGradingPass &color_grading_pass,
                     VignettePass &vignette_pass,
                     FXAAPass &fxaa_pass,
                     ToneMappingPass &tone_mapping_pass,
                     UIPass &ui_pass,
                     CombineUIPass &combine_ui_pass,
                     ParticlePass &particle_pass,
                     uint32_t current_swapchain_image_index);

    void copyNormalAndDepthImage();

    RHIImageView* m_point_light_shadow_color_image_view;
    RHIImageView* m_directional_light_shadow_color_image_view;

    bool                            m_is_show_axis{ false };
    bool                            m_enable_fxaa{ false };
    size_t                          m_selected_axis{ 3 };
    MeshPerframeStorageBufferObject m_mesh_perframe_storage_buffer_object;
    AxisStorageBufferObject         m_axis_storage_buffer_object;

    void updateAfterFramebufferRecreate();

    RHICommandBuffer* getRenderCommandBuffer();

    void setParticlePass(std::shared_ptr<ParticlePass> pass);

private:
    void setupParticlePass();
    void setupAttachments();
    void setupRenderPass();
    void setupDescriptorSetLayout();
    void setupPipelines();
    void setupDescriptorSet();
    void setupFramebufferDescriptorSet();
    void setupSwapchainFramebuffers();

    void setupModelGlobalDescriptorSet();
    void setupSkyboxDescriptorSet();
    void setupAxisDescriptorSet();
    void setupParticleDescriptorSet();
    void setupGbufferLightingDescriptorSet();

    void drawMesh(RenderPipeLineType render_pipeline_type);
    void drawDeferredLighting();
    void drawSkybox();
    void drawAxis();

private:
    // 根据 material 和 mesh 进行重新分组 (re-batch)
    std::map<VulkanPBRMaterial*, std::map<VulkanMesh*, std::vector<MeshNode>>> reorganizeMeshNodes(std::vector<RenderMeshNode> *visible_mesh_nodes);

    std::vector<RHIFramebuffer*> m_swapchain_framebuffers;
    std::shared_ptr<ParticlePass> m_particle_pass;
};
} // namespace Piccolo
