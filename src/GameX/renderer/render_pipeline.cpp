#include "GameX/renderer/render_pipeline.h"

#include "GameX/application/application.h"
#include "GameX/renderer/camera.h"
#include "GameX/renderer/entity.h"
#include "GameX/renderer/renderer.h"
#include "GameX/shaders/shaders.h"

namespace GameX::Base {
RenderPipeline::RenderPipeline(struct Renderer *renderer, int max_film)
    : renderer_(renderer) {
  CreateRenderPass();
  CreateGeometryPass();
  CreateLightingPassCommonAssets(max_film);
  CreateAmbientLightPipeline();
  CreateDirectionalLightPipeline();
}

std::unique_ptr<RenderPipeline::Film> RenderPipeline::CreateFilm(int width,
                                                                 int height) {
  std::unique_ptr<Film> film = std::make_unique<Film>();
  VkExtent2D extent{static_cast<uint32_t>(width),
                    static_cast<uint32_t>(height)};
  film->albedo_image = std::make_unique<grassland::vulkan::Image>(
      renderer_->App()->Core(), VK_FORMAT_R32G32B32A32_SFLOAT, extent,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);
  film->normal_image = std::make_unique<grassland::vulkan::Image>(
      renderer_->App()->Core(), VK_FORMAT_R32G32B32A32_SFLOAT, extent,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);
  film->position_image = std::make_unique<grassland::vulkan::Image>(
      renderer_->App()->Core(), VK_FORMAT_R32G32B32A32_SFLOAT, extent,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);
  film->depth_image = std::make_unique<grassland::vulkan::Image>(
      renderer_->App()->Core(), VK_FORMAT_D32_SFLOAT, extent,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT);

  film->output_image = std::make_unique<grassland::vulkan::Image>(
      renderer_->App()->Core(), VK_FORMAT_R32G32B32A32_SFLOAT, extent,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

  film->framebuffer = std::make_unique<grassland::vulkan::Framebuffer>(
      renderer_->App()->Core(), extent, render_pass_->Handle(),
      std::vector<VkImageView>{
          film->albedo_image->ImageView(), film->normal_image->ImageView(),
          film->position_image->ImageView(), film->depth_image->ImageView(),
          film->output_image->ImageView()});

  film->input_attachment_set =
      std::make_unique<grassland::vulkan::DescriptorSet>(
          renderer_->App()->Core(), lighting_pass_descriptor_pool_.get(),
          lighting_pass_descriptor_set_layout_.get());

  VkDescriptorImageInfo albedo_image_info = {};
  albedo_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  albedo_image_info.imageView = film->albedo_image->ImageView();
  albedo_image_info.sampler = nullptr;

  VkDescriptorImageInfo normal_image_info = {};
  normal_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  normal_image_info.imageView = film->normal_image->ImageView();
  normal_image_info.sampler = nullptr;

  VkDescriptorImageInfo position_image_info = {};
  position_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  position_image_info.imageView = film->position_image->ImageView();
  position_image_info.sampler = nullptr;

  VkWriteDescriptorSet albedo_write_descriptor_set = {};
  albedo_write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  albedo_write_descriptor_set.dstSet = film->input_attachment_set->Handle();
  albedo_write_descriptor_set.dstBinding = 0;
  albedo_write_descriptor_set.dstArrayElement = 0;
  albedo_write_descriptor_set.descriptorType =
      VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  albedo_write_descriptor_set.descriptorCount = 1;
  albedo_write_descriptor_set.pImageInfo = &albedo_image_info;

  VkWriteDescriptorSet normal_write_descriptor_set = {};
  normal_write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  normal_write_descriptor_set.dstSet = film->input_attachment_set->Handle();
  normal_write_descriptor_set.dstBinding = 1;
  normal_write_descriptor_set.dstArrayElement = 0;
  normal_write_descriptor_set.descriptorType =
      VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  normal_write_descriptor_set.descriptorCount = 1;
  normal_write_descriptor_set.pImageInfo = &normal_image_info;

  VkWriteDescriptorSet position_write_descriptor_set = {};
  position_write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  position_write_descriptor_set.dstSet = film->input_attachment_set->Handle();
  position_write_descriptor_set.dstBinding = 2;
  position_write_descriptor_set.dstArrayElement = 0;
  position_write_descriptor_set.descriptorType =
      VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
  position_write_descriptor_set.descriptorCount = 1;
  position_write_descriptor_set.pImageInfo = &position_image_info;

  std::vector<VkWriteDescriptorSet> write_descriptor_sets;
  write_descriptor_sets.push_back(albedo_write_descriptor_set);
  write_descriptor_sets.push_back(normal_write_descriptor_set);
  write_descriptor_sets.push_back(position_write_descriptor_set);

  vkUpdateDescriptorSets(renderer_->App()->Core()->Device()->Handle(),
                         static_cast<uint32_t>(write_descriptor_sets.size()),
                         write_descriptor_sets.data(), 0, nullptr);

  return film;
}

void RenderPipeline::Render(VkCommandBuffer cmd_buffer,
                            const Scene &scene,
                            const Camera &cameras,
                            const RenderPipeline::Film &film) {
  VkRenderPassBeginInfo render_pass_begin_info{};
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.renderPass = render_pass_->Handle();
  render_pass_begin_info.framebuffer = film.framebuffer->Handle();
  render_pass_begin_info.renderArea.offset = {0, 0};
  render_pass_begin_info.renderArea.extent = film.Extent();
  std::array<VkClearValue, 5> clear_values{};
  clear_values[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clear_values[1].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clear_values[2].color = {0.0f, 0.0f, 0.0f, 0.0f};
  clear_values[3].depthStencil = {1.0f, 0};
  clear_values[4].color = {0.0f, 0.0f, 0.0f, 0.0f};
  render_pass_begin_info.clearValueCount =
      static_cast<uint32_t>(clear_values.size());
  render_pass_begin_info.pClearValues = clear_values.data();
  vkCmdBeginRenderPass(cmd_buffer, &render_pass_begin_info,
                       VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    geometry_pass_pipeline_->Handle());

  VkDescriptorSet camera_descriptor_set =
      cameras.DescriptorSet(renderer_->App()->Core()->CurrentFrame())->Handle();
  vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          geometry_pass_pipeline_layout_->Handle(), 0, 1,
                          &camera_descriptor_set, 0, nullptr);

  // Viewport and scissor
  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = film.Extent().width;
  viewport.height = film.Extent().height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = film.Extent();
  vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

  for (auto &entity : scene.Entities()) {
    VkDescriptorSet entity_descriptor_set =
        entity->DescriptorSet(renderer_->App()->Core()->CurrentFrame())
            ->Handle();
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            geometry_pass_pipeline_layout_->Handle(), 1, 1,
                            &entity_descriptor_set, 0, nullptr);

    VkBuffer vertex_buffers[] = {
        entity->Model()
            ->VertexBuffer(renderer_->App()->Core()->CurrentFrame())
            ->Handle()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd_buffer, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(
        cmd_buffer,
        entity->Model()
            ->IndexBuffer(renderer_->App()->Core()->CurrentFrame())
            ->Handle(),
        0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd_buffer, entity->Model()->IndexCount(), 1, 0, 0, 0);
  }

  vkCmdNextSubpass(cmd_buffer, VK_SUBPASS_CONTENTS_INLINE);

  std::vector<Light *> lights;
  for (auto &light : scene.Lights()) {
    lights.push_back(light);
  }

  std::sort(lights.begin(), lights.end(), [](Light *a, Light *b) {
    return a->LightingPipeline() < b->LightingPipeline();
  });

  grassland::vulkan::Pipeline *last_pipeline = nullptr;

  VkDescriptorSet input_attachment_descriptor_set =
      film.input_attachment_set->Handle();

  for (auto &light : lights) {
    if (light->LightingPipeline() != last_pipeline) {
      last_pipeline = light->LightingPipeline();
      vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        light->LightingPipeline()->Handle());
    }

    vkCmdBindDescriptorSets(
        cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        light->LightingPipeline()->Settings().pipeline_layout->Handle(), 0, 1,
        &input_attachment_descriptor_set, 0, nullptr);

    light->Lighting(cmd_buffer, renderer_->App()->Core()->CurrentFrame());
  }

  vkCmdEndRenderPass(cmd_buffer);
}

void RenderPipeline::CreateRenderPass() {
  std::vector<VkAttachmentDescription> attachment_descriptions;

  albedo_attachment_index_ =
      static_cast<uint32_t>(attachment_descriptions.size());
  attachment_descriptions.push_back(VkAttachmentDescription{
      0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
  normal_attachment_index_ =
      static_cast<uint32_t>(attachment_descriptions.size());
  attachment_descriptions.push_back(VkAttachmentDescription{
      0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
  position_attachment_index_ =
      static_cast<uint32_t>(attachment_descriptions.size());
  attachment_descriptions.push_back(VkAttachmentDescription{
      0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
  depth_attachment_index_ =
      static_cast<uint32_t>(attachment_descriptions.size());
  attachment_descriptions.push_back(VkAttachmentDescription{
      0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
  output_attachment_index_ =
      static_cast<uint32_t>(attachment_descriptions.size());
  attachment_descriptions.push_back(VkAttachmentDescription{
      0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});

  std::vector<grassland::vulkan::SubpassSettings> subpass_settings;
  subpass_settings.emplace_back(
      std::vector<VkAttachmentReference>{
          {albedo_attachment_index_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
          {normal_attachment_index_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
          {position_attachment_index_,
           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      },
      std::optional<VkAttachmentReference>(
          {depth_attachment_index_,
           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}),
      std::vector<VkAttachmentReference>{});

  // Add lighting subpass
  subpass_settings.emplace_back(
      std::vector<VkAttachmentReference>{
          {albedo_attachment_index_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
          {normal_attachment_index_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
          {position_attachment_index_,
           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      },
      std::vector<VkAttachmentReference>{
          {output_attachment_index_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
      },
      std::nullopt, std::vector<VkAttachmentReference>{},
      std::vector<uint32_t>{depth_attachment_index_});

  std::vector<VkSubpassDependency> dependencies;
  dependencies.push_back(VkSubpassDependency{
      VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_DEPENDENCY_BY_REGION_BIT});

  dependencies.push_back(VkSubpassDependency{
      0, 1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
      VK_DEPENDENCY_BY_REGION_BIT});

  render_pass_ = std::make_unique<grassland::vulkan::RenderPass>(
      renderer_->App()->Core(), attachment_descriptions, subpass_settings,
      dependencies);
}

void RenderPipeline::CreateGeometryPass() {
  geometry_pass_vertex_shader_ =
      std::make_unique<grassland::vulkan::ShaderModule>(
          renderer_->App()->Core(), BuiltInShaderSpv("geometry_pass.vert"));
  geometry_pass_fragment_shader_ =
      std::make_unique<grassland::vulkan::ShaderModule>(
          renderer_->App()->Core(), BuiltInShaderSpv("geometry_pass.frag"));

  geometry_pass_pipeline_layout_ =
      std::make_unique<grassland::vulkan::PipelineLayout>(
          renderer_->App()->Core(),
          std::vector<grassland::vulkan::DescriptorSetLayout *>{
              renderer_->CameraDescriptorSetLayout(),
              renderer_->EntityDescriptorSetLayout()});

  grassland::vulkan::PipelineSettings geometry_pass_pipeline_settings(
      render_pass_.get(), geometry_pass_pipeline_layout_.get());
  geometry_pass_pipeline_settings.AddShaderStage(
      geometry_pass_vertex_shader_.get(), VK_SHADER_STAGE_VERTEX_BIT);
  geometry_pass_pipeline_settings.AddShaderStage(
      geometry_pass_fragment_shader_.get(), VK_SHADER_STAGE_FRAGMENT_BIT);
  geometry_pass_pipeline_settings.AddInputBinding(0, sizeof(Vertex),
                                                  VK_VERTEX_INPUT_RATE_VERTEX);
  geometry_pass_pipeline_settings.AddInputAttribute(
      0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position));
  geometry_pass_pipeline_settings.AddInputAttribute(
      0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));
  geometry_pass_pipeline_settings.AddInputAttribute(
      0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord));
  geometry_pass_pipeline_settings.AddInputAttribute(
      0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal));
  geometry_pass_pipeline_settings.AddInputAttribute(
      0, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent));

  geometry_pass_pipeline_settings.SetPrimitiveTopology(
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  geometry_pass_pipeline_settings.SetMultiSampleState(VK_SAMPLE_COUNT_1_BIT);

  geometry_pass_pipeline_settings.SetCullMode(VK_CULL_MODE_BACK_BIT);

  geometry_pass_pipeline_ = std::make_unique<grassland::vulkan::Pipeline>(
      renderer_->App()->Core(), geometry_pass_pipeline_settings);
}

void RenderPipeline::CreateLightingPassCommonAssets(int max_film) {
  lighting_pass_descriptor_pool_ =
      std::make_unique<grassland::vulkan::DescriptorPool>(
          renderer_->App()->Core(),
          std::vector<VkDescriptorPoolSize>{
              {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
               static_cast<uint32_t>(max_film *
                                     Film::kNumGBufferInputAttachments)},
          },
          max_film);

  lighting_pass_descriptor_set_layout_ =
      std::make_unique<grassland::vulkan::DescriptorSetLayout>(
          renderer_->App()->Core(),
          std::vector<VkDescriptorSetLayoutBinding>{
              {0, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1,
               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
              {1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1,
               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
              {2, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1,
               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
          });
}

void RenderPipeline::CreateAmbientLightPipeline() {
  ambient_light_vertex_shader_ =
      std::make_unique<grassland::vulkan::ShaderModule>(
          renderer_->App()->Core(),
          BuiltInShaderSpv("fullscreen_lighting_pass.vert"));
  ambient_light_fragment_shader_ =
      std::make_unique<grassland::vulkan::ShaderModule>(
          renderer_->App()->Core(), BuiltInShaderSpv("ambient_light.frag"));

  ambient_light_pipeline_layout_ =
      std::make_unique<grassland::vulkan::PipelineLayout>(
          renderer_->App()->Core(),
          std::vector<grassland::vulkan::DescriptorSetLayout *>{
              lighting_pass_descriptor_set_layout_.get(),
              renderer_->AmbientLightDescriptorSetLayout()});

  grassland::vulkan::PipelineSettings ambient_light_pipeline_settings(
      render_pass_.get(), ambient_light_pipeline_layout_.get(), 1);
  ambient_light_pipeline_settings.AddShaderStage(
      ambient_light_vertex_shader_.get(), VK_SHADER_STAGE_VERTEX_BIT);
  ambient_light_pipeline_settings.AddShaderStage(
      ambient_light_fragment_shader_.get(), VK_SHADER_STAGE_FRAGMENT_BIT);
  ambient_light_pipeline_settings.SetPrimitiveTopology(
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  ambient_light_pipeline_settings.SetMultiSampleState(VK_SAMPLE_COUNT_1_BIT);

  ambient_light_pipeline_settings.SetCullMode(VK_CULL_MODE_NONE);

  ambient_light_pipeline_settings.SetBlendState(
      0, VkPipelineColorBlendAttachmentState{
             VK_TRUE,
             VK_BLEND_FACTOR_ONE,
             VK_BLEND_FACTOR_ONE,
             VK_BLEND_OP_ADD,
             VK_BLEND_FACTOR_ONE,
             VK_BLEND_FACTOR_ZERO,
             VK_BLEND_OP_ADD,
             VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
         });

  ambient_light_pipeline_ = std::make_unique<grassland::vulkan::Pipeline>(
      renderer_->App()->Core(), ambient_light_pipeline_settings);
}

void RenderPipeline::CreateDirectionalLightPipeline() {
  directional_light_vertex_shader_ =
      std::make_unique<grassland::vulkan::ShaderModule>(
          renderer_->App()->Core(),
          BuiltInShaderSpv("fullscreen_lighting_pass.vert"));
  directional_light_fragment_shader_ =
      std::make_unique<grassland::vulkan::ShaderModule>(
          renderer_->App()->Core(), BuiltInShaderSpv("directional_light.frag"));

  directional_light_pipeline_layout_ =
      std::make_unique<grassland::vulkan::PipelineLayout>(
          renderer_->App()->Core(),
          std::vector<grassland::vulkan::DescriptorSetLayout *>{
              lighting_pass_descriptor_set_layout_.get(),
              renderer_->AmbientLightDescriptorSetLayout()});

  grassland::vulkan::PipelineSettings directional_light_pipeline_settings(
      render_pass_.get(), directional_light_pipeline_layout_.get(), 1);
  directional_light_pipeline_settings.AddShaderStage(
      directional_light_vertex_shader_.get(), VK_SHADER_STAGE_VERTEX_BIT);
  directional_light_pipeline_settings.AddShaderStage(
      directional_light_fragment_shader_.get(), VK_SHADER_STAGE_FRAGMENT_BIT);
  directional_light_pipeline_settings.SetPrimitiveTopology(
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  directional_light_pipeline_settings.SetMultiSampleState(
      VK_SAMPLE_COUNT_1_BIT);

  directional_light_pipeline_settings.SetCullMode(VK_CULL_MODE_NONE);

  directional_light_pipeline_settings.SetBlendState(
      0, VkPipelineColorBlendAttachmentState{
             VK_TRUE,
             VK_BLEND_FACTOR_ONE,
             VK_BLEND_FACTOR_ONE,
             VK_BLEND_OP_ADD,
             VK_BLEND_FACTOR_ONE,
             VK_BLEND_FACTOR_ZERO,
             VK_BLEND_OP_ADD,
             VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
         });

  directional_light_pipeline_ = std::make_unique<grassland::vulkan::Pipeline>(
      renderer_->App()->Core(), directional_light_pipeline_settings);
}
}  // namespace GameX::Base