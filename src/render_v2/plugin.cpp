#include <rex/android_diagnostics.h>
#include <rex/logging.h>
#include <rex/platform/dynlib.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

#include "pattern_spirv.h"

namespace {
using rex::X_STATUS;
using rex::ui::vulkan::VulkanPresenter;
constexpr VkExtent2D kPatternExtent{960, 540};

class PatternRenderer {
 public:
  PatternRenderer(VulkanPresenter& presenter, const rex::ui::vulkan::VulkanDevice& gpu, bool solid)
      : presenter_(presenter), gpu_(gpu), vk_(gpu_.functions()), solid_(solid) {}

  ~PatternRenderer() {
    if (command_pool_) vk_.vkDestroyCommandPool(gpu_.device(), command_pool_, nullptr);
    if (fence_) vk_.vkDestroyFence(gpu_.device(), fence_, nullptr);
    if (pipeline_) vk_.vkDestroyPipeline(gpu_.device(), pipeline_, nullptr);
    if (layout_) vk_.vkDestroyPipelineLayout(gpu_.device(), layout_, nullptr);
    if (pass_) vk_.vkDestroyRenderPass(gpu_.device(), pass_, nullptr);
    if (descriptors_) vk_.vkDestroyDescriptorPool(gpu_.device(), descriptors_, nullptr);
    if (descriptor_layout_)
      vk_.vkDestroyDescriptorSetLayout(gpu_.device(), descriptor_layout_, nullptr);
    if (texture_view_) vk_.vkDestroyBufferView(gpu_.device(), texture_view_, nullptr);
    if (texture_) vk_.vkDestroyBuffer(gpu_.device(), texture_, nullptr);
    if (texture_memory_) vk_.vkFreeMemory(gpu_.device(), texture_memory_, nullptr);
  }

  bool Initialize() {
    const auto device = gpu_.device();
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = gpu_.queue_family_graphics_compute();
    if (vk_.vkCreateCommandPool(device, &pool, nullptr, &command_pool_) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo buffers{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    buffers.commandPool = command_pool_;
    buffers.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    buffers.commandBufferCount = 1;
    if (vk_.vkAllocateCommandBuffers(device, &buffers, &command_buffer_) != VK_SUCCESS)
      return false;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vk_.vkCreateFence(device, &fence, nullptr, &fence_) != VK_SUCCESS) return false;
    return CreateTexture() && CreatePipeline();
  }

  bool Present() {
    return presenter_.RefreshGuestOutput(
        kPatternExtent.width, kPatternExtent.height, 16, 9,
        [this](rex::ui::Presenter::GuestOutputRefreshContext& context) {
          return Draw(static_cast<VulkanPresenter::VulkanGuestOutputRefreshContext&>(context));
        });
  }

 private:
  bool CreateTexture() {
    const auto device = gpu_.device();
    VkFormatProperties format{};
    gpu_.vulkan_instance()->functions().vkGetPhysicalDeviceFormatProperties(
        gpu_.physical_device(), VK_FORMAT_R32_UINT, &format);
    if (!(format.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)) {
      REXLOG_ERROR("[tex] backend=render-v2 format=R32_UINT unsupported=uniform_texel_buffer");
      return false;
    }
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = 64 * sizeof(uint32_t);
    buffer.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk_.vkCreateBuffer(device, &buffer, nullptr, &texture_) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vk_.vkGetBufferMemoryRequirements(device, texture_, &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    gpu_.vulkan_instance()->functions().vkGetPhysicalDeviceMemoryProperties(gpu_.physical_device(),
                                                                            &memory);
    uint32_t type = 0;
    constexpr auto flags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (; type < memory.memoryTypeCount; ++type) {
      if ((requirements.memoryTypeBits & (1u << type)) &&
          (memory.memoryTypes[type].propertyFlags & flags) == flags)
        break;
    }
    if (type == memory.memoryTypeCount) return false;
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = type;
    if (vk_.vkAllocateMemory(device, &alloc, nullptr, &texture_memory_) != VK_SUCCESS ||
        vk_.vkBindBufferMemory(device, texture_, texture_memory_, 0) != VK_SUCCESS)
      return false;
    void* pixels = nullptr;
    if (vk_.vkMapMemory(device, texture_memory_, 0, buffer.size, 0, &pixels) != VK_SUCCESS)
      return false;
    auto* texels = static_cast<uint32_t*>(pixels);
    for (unsigned y = 0; y < 8; ++y) {
      for (unsigned x = 0; x < 8; ++x) texels[y * 8 + x] = ((x / 2) + (y / 2)) % 2;
    }
    vk_.vkUnmapMemory(device, texture_memory_);
    VkBufferViewCreateInfo view{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    view.buffer = texture_;
    view.format = VK_FORMAT_R32_UINT;
    view.range = buffer.size;
    if (vk_.vkCreateBufferView(device, &view, nullptr, &texture_view_) != VK_SUCCESS) return false;

    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1,
                                               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptor_layout{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptor_layout.bindingCount = 1;
    descriptor_layout.pBindings = &binding;
    if (vk_.vkCreateDescriptorSetLayout(device, &descriptor_layout, nullptr, &descriptor_layout_) !=
        VK_SUCCESS)
      return false;
    const VkDescriptorPoolSize pool_size{binding.descriptorType, 1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &pool_size;
    if (vk_.vkCreateDescriptorPool(device, &pool, nullptr, &descriptors_) != VK_SUCCESS)
      return false;
    VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sets.descriptorPool = descriptors_;
    sets.descriptorSetCount = 1;
    sets.pSetLayouts = &descriptor_layout_;
    if (vk_.vkAllocateDescriptorSets(device, &sets, &texture_set_) != VK_SUCCESS) return false;
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture_set_;
    write.descriptorType = binding.descriptorType;
    write.descriptorCount = 1;
    write.pTexelBufferView = &texture_view_;
    vk_.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    REXLOG_INFO(
        "[tex] backend=render-v2 format=R32_UINT address=host width=8 height=8 pitch=32 "
        "fallback=none");
    return true;
  }

  bool CreatePipeline() {
    const auto device = gpu_.device();
    VkAttachmentDescription attachment{};
    attachment.format = VulkanPresenter::kGuestOutputFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VulkanPresenter::kGuestOutputInternalLayout;
    const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    const std::array<VkSubpassDependency, 2> dependencies{{
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         VK_ACCESS_SHADER_READ_BIT, 0},
    }};
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = uint32_t(dependencies.size());
    pass.pDependencies = dependencies.data();
    if (vk_.vkCreateRenderPass(device, &pass, nullptr, &pass_) != VK_SUCCESS) return false;

    const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int32_t)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &descriptor_layout_;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (vk_.vkCreatePipelineLayout(device, &layout, nullptr, &layout_) != VK_SUCCESS) return false;

    std::array<VkPipelineShaderStageCreateInfo, 2> shaders{};
    const uint32_t* code[] = {kPatternVertex, kPatternFragment};
    const size_t sizes[] = {sizeof(kPatternVertex), sizeof(kPatternFragment)};
    for (size_t i = 0; i < shaders.size(); ++i) {
      VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
      module.pCode = code[i];
      module.codeSize = sizes[i];
      shaders[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      shaders[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
      shaders[i].pName = "main";
      if (vk_.vkCreateShaderModule(device, &module, nullptr, &shaders[i].module) != VK_SUCCESS) {
        if (i) vk_.vkDestroyShaderModule(device, shaders[0].module, nullptr);
        return false;
      }
    }
    VkPipelineVertexInputStateCreateInfo vertices{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    const VkViewport viewport{0, 0, float(kPatternExtent.width), float(kPatternExtent.height),
                              0, 1};
    const VkRect2D scissor{{0, 0}, kPatternExtent};
    VkPipelineViewportStateCreateInfo view{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    view.viewportCount = 1;
    view.pViewports = &viewport;
    view.scissorCount = 1;
    view.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = uint32_t(shaders.size());
    pipeline.pStages = shaders.data();
    pipeline.pVertexInputState = &vertices;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &view;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples;
    pipeline.pColorBlendState = &blend;
    pipeline.layout = layout_;
    pipeline.renderPass = pass_;
    const auto start = std::chrono::steady_clock::now();
    const auto result =
        vk_.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_);
    for (const auto& shader : shaders) vk_.vkDestroyShaderModule(device, shader.module, nullptr);
    REXLOG_INFO(
        "[pipe] backend=render-v2 created={} ms={:.3f} result={}", result == VK_SUCCESS,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
        int(result));
    return result == VK_SUCCESS;
  }

  bool Draw(VulkanPresenter::VulkanGuestOutputRefreshContext& context) {
    const auto device = gpu_.device();
    VkImageView attachment = context.image_view();
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = pass_;
    fb.attachmentCount = 1;
    fb.pAttachments = &attachment;
    fb.width = kPatternExtent.width;
    fb.height = kPatternExtent.height;
    fb.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (vk_.vkCreateFramebuffer(device, &fb, nullptr, &framebuffer) != VK_SUCCESS) return false;
    const bool success = Submit(framebuffer);
    vk_.vkDestroyFramebuffer(device, framebuffer, nullptr);
    return success;
  }

  bool Submit(VkFramebuffer framebuffer) {
    if (vk_.vkResetCommandPool(gpu_.device(), command_pool_, 0) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    commands.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vk_.vkBeginCommandBuffer(command_buffer_, &commands) != VK_SUCCESS) return false;
    const VkClearValue clear{{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = pass_;
    pass.framebuffer = framebuffer;
    pass.renderArea.extent = kPatternExtent;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    vk_.vkCmdBeginRenderPass(command_buffer_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vk_.vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vk_.vkCmdBindDescriptorSets(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                                &texture_set_, 0, nullptr);
    const int32_t first_draw = solid_ ? 2 : 0;
    for (int32_t draw = first_draw; draw < (solid_ ? 3 : 2); ++draw) {
      vk_.vkCmdPushConstants(command_buffer_, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                             sizeof(draw), &draw);
      vk_.vkCmdDraw(command_buffer_, 3, 1, draw == 1 ? 3u : 0u, 0);
    }
    vk_.vkCmdEndRenderPass(command_buffer_);
    if (vk_.vkEndCommandBuffer(command_buffer_) != VK_SUCCESS ||
        vk_.vkResetFences(gpu_.device(), 1, &fence_) != VK_SUCCESS)
      return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer_;
    {
      const auto queue = gpu_.AcquireQueue(gpu_.queue_family_graphics_compute(), 0);
      if (vk_.vkQueueSubmit(queue.queue(), 1, &submit, fence_) != VK_SUCCESS) return false;
    }
    return vk_.vkWaitForFences(gpu_.device(), 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
  }

  VulkanPresenter& presenter_;
  const rex::ui::vulkan::VulkanDevice& gpu_;
  const rex::ui::vulkan::VulkanDevice::Functions& vk_;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkRenderPass pass_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkBuffer texture_ = VK_NULL_HANDLE;
  VkDeviceMemory texture_memory_ = VK_NULL_HANDLE;
  VkBufferView texture_view_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptors_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
  const bool solid_;
  VkDescriptorSet texture_set_ = VK_NULL_HANDLE;
};

class RenderV2 final : public rex::system::IGraphicsSystem {
 public:
  rex::X_STATUS SetupPresentation(rex::ui::WindowedAppContext*) override {
    if (presenter_) return X_STATUS_SUCCESS;
    provider_ = rex::ui::vulkan::VulkanProvider::Create(false, true);
    if (!provider_) return X_STATUS_UNSUCCESSFUL;
    auto presenter = provider_->CreatePresenter();
    if (!presenter) return X_STATUS_UNSUCCESSFUL;
    presenter_.reset(static_cast<VulkanPresenter*>(presenter.release()));
    return X_STATUS_SUCCESS;
  }

  rex::X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                              rex::system::KernelState* kernel) override {
    if (!presenter_ || bool(dispatcher) != bool(kernel)) return X_STATUS_UNSUCCESSFUL;
    if (worker_.joinable()) return X_STATUS_SUCCESS;
    guest_mode_ = dispatcher != nullptr;
    if (guest_mode_) {
      using Create = rex::system::IGraphicsSystem* (*)(uint32_t, void (*)(void*), void*);
      if (!frontend_library_.Load("librexgpu-xenos.so",
                                  rex::platform::SymbolResolution::kImmediate)) {
        REXLOG_ERROR("[gpu-null] frontend_load=failed");
        return X_STATUS_UNSUCCESSFUL;
      }
      const auto create = frontend_library_.GetSymbol<Create>("rex_gpu_create_null_guest");
      if (create) frontend_.reset(create(rex::system::kGpuPluginAbiVersion, NotifySwap, this));
      if (!frontend_) {
        REXLOG_ERROR("[gpu-null] frontend_create=failed");
        return X_STATUS_UNSUCCESSFUL;
      }
      const auto status = frontend_->SetupGuestGpu(dispatcher, kernel);
      if (XFAILED(status)) return status;
      REXLOG_INFO("[gpu-null] frontend=rexglue-xenos raster=off resolves=off");
    }
    worker_ = std::jthread([this](std::stop_token stop) { PresentPattern(stop); });
    return X_STATUS_SUCCESS;
  }

  ~RenderV2() override { Shutdown(); }
  bool has_presentation() const override { return bool(presenter_); }
  rex::ui::GraphicsProvider* provider() const override { return provider_.get(); }
  rex::ui::Presenter* presenter() const override { return presenter_.get(); }

  void SetInterruptCallback(uint32_t callback, uint32_t data) override {
    if (frontend_) frontend_->SetInterruptCallback(callback, data);
  }
  void InitializeRingBuffer(uint32_t pointer, uint32_t size_log2) override {
    if (frontend_) frontend_->InitializeRingBuffer(pointer, size_log2);
  }
  void EnableReadPointerWriteBack(uint32_t pointer, uint32_t size_log2) override {
    if (frontend_) frontend_->EnableReadPointerWriteBack(pointer, size_log2);
  }
  uint32_t edram_tile_count() const override {
    return frontend_ ? frontend_->edram_tile_count() : 0;
  }

  void Shutdown() override {
    if (worker_.joinable()) {
      worker_.request_stop();
      worker_.join();
    }
    if (frontend_) {
      frontend_->Shutdown();
      frontend_.reset();
    }
  }

 private:
  static void NotifySwap(void* context) {
    auto& self = *static_cast<RenderV2*>(context);
    {
      std::lock_guard lock(self.swap_mutex_);
      ++self.guest_swaps_;
    }
    self.swap_wake_.notify_one();
  }

  void PresentPattern(std::stop_token stop) {
    using namespace std::chrono;
    const char* phase = guest_mode_ ? "F2" : "F1";
    const auto* device = provider_->vulkan_device();
    if (!device || presenter_->vulkan_device() != device) {
      REXLOG_ERROR("[present] backend=render-v2 phase={} device_binding=failed", phase);
      rex::FlushLogging();
      return;
    }
    REXLOG_INFO("[present] backend=render-v2 phase={} device_binding=ok", phase);
    PatternRenderer renderer(*presenter_, *device, guest_mode_);
    if (!renderer.Initialize()) {
      REXLOG_ERROR("[present] backend=render-v2 pattern_init=failed");
      return;
    }
    std::mutex mutex;
    std::condition_variable_any timer;
    std::unique_lock lock(mutex);
    uint64_t consumed = 0;
    unsigned submitted = 0;
    unsigned failed = 0;
    auto report = steady_clock::now() + seconds(1);
    auto last_report = steady_clock::now();
    auto previous_presented = rex::GetAndroidDiagnostics().presented.load();
    while (!stop.stop_requested()) {
      uint64_t swaps = 0;
      if (guest_mode_) {
        std::unique_lock swap_lock(swap_mutex_);
        swap_wake_.wait_until(swap_lock, stop, report, [&] { return guest_swaps_ != consumed; });
        swaps = guest_swaps_;
      }
      if (stop.stop_requested()) break;
      if (!guest_mode_ || swaps != consumed || (!submitted && !failed)) {
        consumed = swaps;
        if (renderer.Present())
          ++submitted;
        else
          ++failed;
      }
      const auto now = steady_clock::now();
      if (now >= report) {
        const auto presented = rex::GetAndroidDiagnostics().presented.load();
        const auto fps =
            double(presented - previous_presented) / duration<double>(now - last_report).count();
        REXLOG_INFO(
            "[present] backend=render-v2 phase={} guest_swaps={} submitted={} presented={} "
            "failed={}",
            phase, swaps, submitted, presented, failed);
        if (!guest_mode_)
          REXLOG_INFO("host presentation (recent 2s): {:.2f} FPS, phase=F1 game_updates=0", fps);
        previous_presented = presented;
        last_report = now;
        report = now + seconds(1);
        rex::FlushLogging();
      }
      if (!guest_mode_) timer.wait_for(lock, stop, milliseconds(33), [] { return false; });
    }
  }

  std::unique_ptr<rex::ui::vulkan::VulkanProvider> provider_;
  std::unique_ptr<VulkanPresenter> presenter_;
  rex::platform::DynamicLibrary frontend_library_;
  std::unique_ptr<rex::system::IGraphicsSystem> frontend_;
  bool guest_mode_ = false;
  std::mutex swap_mutex_;
  std::condition_variable_any swap_wake_;
  uint64_t guest_swaps_ = 0;
  std::jthread worker_;
};
}  // namespace

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version() {
  return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* rex_gpu_create(
    uint32_t abi, const rex::system::GpuCreateInfo* info) {
  if (abi != rex::system::kGpuPluginAbiVersion || !info || info->struct_size < sizeof(*info))
    return nullptr;
  const std::string_view backend = info->backend ? info->backend : "any";
  return backend == "vulkan" || backend == "any" ? new RenderV2 : nullptr;
}
