// Adapted from DarthMDev/fm3-recomp tools/macos-test/main.cpp.
// Reuse: Vulkan fill/fence/readback, known-color capture, and window lifecycle.
// FM3-specific readback-slot assertions require SDK fixes and are excluded here.
// Game-independent ARM64 SDK check: Vulkan device creation and GPU readback.
#include <bit>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <SDL3/SDL.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/ui_drawer.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>

REXCVAR_DEFINE_BOOL(test_window, false, "Mac SDK Tests",
                    "Open a window and render a triangle before exiting");
REXCVAR_DEFINE_BOOL(test_resize, false, "Mac SDK Tests",
                    "Resize the standalone presentation test three times");
REXCVAR_DEFINE_UINT32(test_window_hold_seconds, 0, "Mac SDK Tests",
                      "Keep the triangle visible for up to 120 seconds for visual inspection");

namespace {
void Check(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
  }
}

struct Resources {
  const rex::ui::vulkan::VulkanDevice& gpu;
  PFN_vkDeviceWaitIdle wait_idle;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  void* mapped = nullptr;
  bool submitted = false;

  ~Resources() {
    const auto& fn = gpu.functions();
    const auto device = gpu.device();
    if (submitted) wait_idle(device);
    if (mapped) fn.vkUnmapMemory(device, memory);
    if (framebuffer) fn.vkDestroyFramebuffer(device, framebuffer, nullptr);
    if (render_pass) fn.vkDestroyRenderPass(device, render_pass, nullptr);
    if (fence) fn.vkDestroyFence(device, fence, nullptr);
    if (pool) fn.vkDestroyCommandPool(device, pool, nullptr);
    if (buffer) fn.vkDestroyBuffer(device, buffer, nullptr);
    if (memory) fn.vkFreeMemory(device, memory, nullptr);
  }
};

void CheckReadback(const rex::ui::vulkan::VulkanDevice& gpu) {
  constexpr uint32_t kWordCount = 256;
  constexpr uint32_t kPattern = 0x4d53084d;
  const auto device = gpu.device();
  const auto& fn = gpu.functions();
  const auto get_proc = gpu.vulkan_instance()->functions().vkGetDeviceProcAddr;
  const auto wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(get_proc(device, "vkDeviceWaitIdle"));
  const auto fill_buffer = reinterpret_cast<PFN_vkCmdFillBuffer>(get_proc(device, "vkCmdFillBuffer"));
  if (!wait_idle || !fill_buffer) throw std::runtime_error("Required Vulkan core entry points missing");
  Resources resources{gpu, wait_idle};

  VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer_info.size = kWordCount * sizeof(uint32_t);
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  Check(fn.vkCreateBuffer(device, &buffer_info, nullptr, &resources.buffer), "vkCreateBuffer");
  VkMemoryRequirements requirements{};
  fn.vkGetBufferMemoryRequirements(device, resources.buffer, &requirements);
  const uint32_t memory_types = requirements.memoryTypeBits & gpu.memory_types().host_visible;
  if (!memory_types) throw std::runtime_error("No host-visible buffer memory type");
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = std::countr_zero(memory_types);
  Check(fn.vkAllocateMemory(device, &allocation, nullptr, &resources.memory), "vkAllocateMemory");
  Check(fn.vkBindBufferMemory(device, resources.buffer, resources.memory, 0), "vkBindBufferMemory");
  Check(fn.vkMapMemory(device, resources.memory, 0, VK_WHOLE_SIZE, 0, &resources.mapped), "vkMapMemory");
  auto* initial_words = static_cast<uint32_t*>(resources.mapped);
  for (uint32_t i = 0; i < kWordCount; ++i) initial_words[i] = 0;
  VkMappedMemoryRange initial_range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  initial_range.memory = resources.memory;
  initial_range.size = VK_WHOLE_SIZE;
  Check(fn.vkFlushMappedMemoryRanges(device, 1, &initial_range), "vkFlushMappedMemoryRanges");

  VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.queueFamilyIndex = gpu.queue_family_graphics_compute();
  Check(fn.vkCreateCommandPool(device, &pool_info, nullptr, &resources.pool), "vkCreateCommandPool");
  VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_info.commandPool = resources.pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  VkCommandBuffer command = VK_NULL_HANDLE;
  Check(fn.vkAllocateCommandBuffers(device, &command_info, &command), "vkAllocateCommandBuffers");
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  Check(fn.vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
  fill_buffer(command, resources.buffer, 0, buffer_info.size, kPattern);
  VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = resources.buffer;
  barrier.size = VK_WHOLE_SIZE;
  fn.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                          0, 0, nullptr, 1, &barrier, 0, nullptr);
  Check(fn.vkEndCommandBuffer(command), "vkEndCommandBuffer");
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  Check(fn.vkCreateFence(device, &fence_info, nullptr, &resources.fence), "vkCreateFence");
  VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submission.commandBufferCount = 1;
  submission.pCommandBuffers = &command;
  // The command is deliberately not submitted yet. Mapped allocation and
  // capacity must not make this producing generation readable.
  if (fn.vkGetFenceStatus(device, resources.fence) != VK_NOT_READY)
    throw std::runtime_error("Test producer fence unexpectedly signaled before submission");
  {
    auto queue = gpu.AcquireQueue(pool_info.queueFamilyIndex, 0);
    Check(fn.vkQueueSubmit(queue.queue(), 1, &submission, resources.fence), "vkQueueSubmit");
    resources.submitted = true;
  }
  Check(fn.vkWaitForFences(device, 1, &resources.fence, VK_TRUE, 5'000'000'000), "vkWaitForFences");
  VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  range.memory = resources.memory;
  range.size = VK_WHOLE_SIZE;
  Check(fn.vkInvalidateMappedMemoryRanges(device, 1, &range), "vkInvalidateMappedMemoryRanges");
  const auto* words = static_cast<const uint32_t*>(resources.mapped);
  for (uint32_t i = 0; i < kWordCount; ++i) {
    if (words[i] != kPattern) throw std::runtime_error("GPU readback mismatch at word " + std::to_string(i));
  }
  std::cout << "GPU fill, completion fence, and CPU readback passed (256 words).\n";
}

class TriangleDrawer final : public rex::ui::UIDrawer {
 public:
  TriangleDrawer(rex::ui::ImmediateDrawer& drawer, rex::ui::Presenter& presenter,
                 rex::ui::SDLWindowedAppContext& context)
      : drawer_(drawer), presenter_(presenter), context_(context),
        close_after_(std::chrono::steady_clock::now() +
                     std::chrono::seconds(REXCVAR_GET(test_window_hold_seconds))) {}

  void Draw(rex::ui::UIDrawContext& context) override {
    const rex::ui::ImmediateVertex vertices[] = {
        {320.0f, 60.0f, 0.0f, 0.0f, 0xff4040ff},
        {540.0f, 300.0f, 0.0f, 0.0f, 0xff40ff40},
        {100.0f, 300.0f, 0.0f, 0.0f, 0xffff4040},
    };
    drawer_.Begin(context, 640.0f, 360.0f);
    rex::ui::ImmediateDrawBatch batch;
    batch.vertices = vertices;
    batch.vertex_count = 3;
    drawer_.BeginDrawBatch(batch);
    rex::ui::ImmediateDraw draw;
    draw.count = 3;
    drawer_.Draw(draw);
    drawer_.EndDrawBatch();
    drawer_.End();
    ++frames_;
    if (REXCVAR_GET(test_resize) && (frames_ == 30 || frames_ == 60 || frames_ == 90)) {
      const unsigned stage = frames_ / 30;
      context_.CallInUIThreadDeferred([this, stage] {
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        if (!windows || count != 1 || !SDL_SetWindowSize(windows[0],
              stage == 1 ? 720 : (stage == 2 ? 800 : 640),
              stage == 1 ? 405 : (stage == 2 ? 450 : 360))) {
          resize_failed_.store(true);
        } else {
          ++resize_count_;
        }
        SDL_free(windows);
      });
    }
    if (frames_ >= 120 && std::chrono::steady_clock::now() >= close_after_) {
      context_.RequestDeferredQuit();
    } else {
      presenter_.RequestUIPaintFromUIThread();
    }
  }

  unsigned frames() const { return frames_; }
  unsigned resize_count() const { return resize_count_.load(); }
  bool resize_failed() const { return resize_failed_.load(); }

 private:
  rex::ui::ImmediateDrawer& drawer_;
  rex::ui::Presenter& presenter_;
  rex::ui::SDLWindowedAppContext& context_;
  unsigned frames_ = 0;
  std::atomic<unsigned> resize_count_{0};
  std::atomic<bool> resize_failed_{false};
  std::chrono::steady_clock::time_point close_after_;
};

void CheckGuestOutput(rex::ui::Presenter& presenter,
                      const rex::ui::vulkan::VulkanDevice& gpu) {
  using VulkanPresenter = rex::ui::vulkan::VulkanPresenter;
  const auto& fn = gpu.functions();
  const auto device = gpu.device();
  const auto wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
      gpu.vulkan_instance()->functions().vkGetDeviceProcAddr(device, "vkDeviceWaitIdle"));
  Resources resources{gpu, wait_idle};
  const bool refreshed = presenter.RefreshGuestOutput(64, 64, 16, 9,
      [&](rex::ui::Presenter::GuestOutputRefreshContext& generic) {
    auto& output = static_cast<VulkanPresenter::VulkanGuestOutputRefreshContext&>(generic);
    VkAttachmentDescription attachment{};
    attachment.format = VulkanPresenter::kGuestOutputFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VulkanPresenter::kGuestOutputInternalLayout;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 1; pass.pAttachments = &attachment;
    pass.subpassCount = 1; pass.pSubpasses = &subpass;
    Check(fn.vkCreateRenderPass(device, &pass, nullptr, &resources.render_pass), "vkCreateRenderPass");
    const auto view = output.image_view();
    VkFramebufferCreateInfo frame{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    frame.renderPass = resources.render_pass;
    frame.attachmentCount = 1; frame.pAttachments = &view;
    frame.width = 64; frame.height = 64; frame.layers = 1;
    Check(fn.vkCreateFramebuffer(device, &frame, nullptr, &resources.framebuffer), "vkCreateFramebuffer");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = gpu.queue_family_graphics_compute();
    Check(fn.vkCreateCommandPool(device, &pool, nullptr, &resources.pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = resources.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
    VkCommandBuffer command;
    Check(fn.vkAllocateCommandBuffers(device, &allocate, &command), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(fn.vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    VkClearValue clear{};
    clear.color.float32[0] = 0.1f; clear.color.float32[1] = 0.2f;
    clear.color.float32[2] = 0.4f; clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo draw{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    draw.renderPass = resources.render_pass; draw.framebuffer = resources.framebuffer;
    draw.renderArea.extent = {64, 64}; draw.clearValueCount = 1; draw.pClearValues = &clear;
    fn.vkCmdBeginRenderPass(command, &draw, VK_SUBPASS_CONTENTS_INLINE);
    fn.vkCmdEndRenderPass(command);
    Check(fn.vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(fn.vkCreateFence(device, &fence, nullptr, &resources.fence), "vkCreateFence");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
    {
      const auto queue = gpu.AcquireQueue(gpu.queue_family_graphics_compute(), 0);
      Check(fn.vkQueueSubmit(queue.queue(), 1, &submit, resources.fence), "vkQueueSubmit");
      resources.submitted = true;
    }
    Check(fn.vkWaitForFences(device, 1, &resources.fence, VK_TRUE, 5000000000ull), "vkWaitForFences");
    return true;
  });
  if (!refreshed) throw std::runtime_error("Known-color guest output refresh failed");
  rex::ui::RawImage image;
  if (!presenter.CaptureGuestOutput(image) || image.width != 64 || image.height != 64 ||
      image.data.size() < 4 || image.data[0] < 24 || image.data[0] > 27 ||
      image.data[1] < 49 || image.data[1] > 53 || image.data[2] < 100 || image.data[2] > 104)
    throw std::runtime_error("Known-color guest image readback failed");
  std::cout << "Known-color guest image refresh and capture passed.\n";
}

void CheckWindow(rex::ui::vulkan::VulkanProvider& provider,
                 rex::ui::SDLWindowedAppContext& context) {
  auto window = rex::ui::Window::Create(context, "FM4 macOS renderer test", 640, 360);
  if (!window || !window->Open()) throw std::runtime_error("Window creation failed");
  std::atomic<bool> gpu_lost{false};
  auto presenter = provider.CreatePresenter([&](bool, bool) {
    gpu_lost.store(true);
    context.RequestDeferredQuit();
  });
  auto immediate = provider.CreateImmediateDrawer();
  if (!presenter || !immediate) throw std::runtime_error("Vulkan presentation setup failed");
  immediate->SetPresenter(presenter.get());
  window->SetPresenter(presenter.get());
  CheckGuestOutput(*presenter, *provider.vulkan_device());
  TriangleDrawer triangle(*immediate, *presenter, context);
  presenter->AddUIDrawerFromUIThread(&triangle, 0);
  presenter->RequestUIPaintFromUIThread();

  std::atomic<bool> stopped{false}, timed_out{false};
  std::thread deadline([&] {
    const auto limit = std::chrono::steady_clock::now() +
                       std::chrono::seconds(10 + REXCVAR_GET(test_window_hold_seconds));
    while (!stopped.load() && std::chrono::steady_clock::now() < limit) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!stopped.load()) {
      timed_out.store(true);
      context.RequestDeferredQuit();
    }
  });
  const int result = context.RunMainMessageLoop();
  stopped.store(true);
  deadline.join();
  const auto& gpu = *provider.vulkan_device();
  const auto wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
      gpu.vulkan_instance()->functions().vkGetDeviceProcAddr(gpu.device(), "vkDeviceWaitIdle"));
  if (!wait_idle) throw std::runtime_error("vkDeviceWaitIdle unavailable during window cleanup");
  Check(wait_idle(gpu.device()), "vkDeviceWaitIdle");
  presenter->RemoveUIDrawerFromUIThread(&triangle);
  immediate->SetPresenter(nullptr);
  window->SetPresenter(nullptr);
  if (result || timed_out.load() || gpu_lost.load() || triangle.frames() < 120 ||
      triangle.resize_failed() || (REXCVAR_GET(test_resize) && triangle.resize_count() != 3)) {
    throw std::runtime_error("Window rendering did not complete 120 frames");
  }
  std::cout << "SDL/MoltenVK window test completed " << triangle.frames()
            << " triangle draws, " << triangle.resize_count() << " resize requests, and exited cleanly.\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    rex::cvar::Init(argc, argv);
    rex::cvar::ApplyEnvironment();
    if (REXCVAR_GET(test_window_hold_seconds) > 120)
      throw std::runtime_error("test_window_hold_seconds must be at most 120");
    rex::InitLoggingEarly();
    std::unique_ptr<rex::ui::SDLWindowedAppContext> context;
    const bool with_window = REXCVAR_GET(test_window);
    if (with_window) {
      context = std::make_unique<rex::ui::SDLWindowedAppContext>();
      if (!context->Initialize()) throw std::runtime_error("SDL initialization failed");
    }
    auto provider = rex::ui::vulkan::VulkanProvider::Create(true, with_window);
    if (!provider) throw std::runtime_error("SDK Vulkan device creation failed");
    const auto& gpu = *provider->vulkan_device();
    std::cout << "SDK GPU device: " << gpu.properties().deviceName << '\n';
    std::cout << "Pixel interlock: " << gpu.properties().fragmentShaderPixelInterlock
              << "; sample interlock: " << gpu.properties().fragmentShaderSampleInterlock << '\n';
    CheckReadback(gpu);
    if (context) CheckWindow(*provider, *context);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "SDK GPU test failed: " << error.what() << '\n';
    return 1;
  }
}
