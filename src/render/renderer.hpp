#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <volk.h>

namespace sandbox {
void vk_check(VkResult result, const char* operation);
struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    void* mapped{};
};
struct Texture {
    VkImage image{};
    VmaAllocation allocation{};
    VkImageView view{};
    VkSampler sampler{};
    VkDescriptorSet descriptor{};
};
class Renderer {
  public:
    static constexpr uint32_t frames_in_flight = 2;
    static constexpr VkFormat scene_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // A null window selects real offscreen rendering, without a surface or swapchain.
    void initialize(SDL_Window* window, bool validation, VkExtent2D offscreen_extent = {1280, 900});
    bool begin_frame();
    void begin_rendering(VkImageView depth = VK_NULL_HANDLE,
                         std::array<float, 4> sky = {0.48f, 0.69f, 0.86f, 1.0f}, bool preserve_depth = false);
    void finish_uploads(); // Timestamp boundary before any world/shadow render pass.
    // Explicit diagnostic mode only. Labels must be string literals; no GPU waits per marker.
    void enable_gpu_profile() { gpu_profile_enabled_ = true; }
    struct CpuFrameTiming {
        double fence_wait_ms{}, acquire_ms{}, submit_ms{}, present_ms{}, idle_wait_ms{};
        uint64_t waited_serial{};
        bool fence_pending{};
    };
    void enable_frame_profile() { frame_profile_enabled_ = true; }
    void reset_cpu_frame_timing() { cpu_frame_timing_ = {}; }
    const CpuFrameTiming& cpu_frame_timing() const { return cpu_frame_timing_; }
    uint64_t submitted_serial() const { return submitted_serial_; }
    void gpu_mark(const char* completed_stage);
    void gpu_profile_frame(bool steady, uint32_t columns, uint32_t chunks, uint32_t triangles);
    void save_gpu_profile(const std::filesystem::path& path);
    void begin_ui();
    // World-only HDR target; UI/capture use either swapchain or owned offscreen images.
    void set_world_target(VkImage image, VkImageView view) {
        world_image_ = image;
        world_view_ = view;
    }
    // Outside rendering: copy opaque colour/depth to already transitioned destinations.
    void snapshot_scene(VkImage colour, VkImage depth, VkImage depth_copy);
    void resume_world(VkImageView depth);
    // Returns capture success; filesystem/PNG failures are logged without ending the game.
    bool end_frame(const std::filesystem::path& capture = {});
    void request_resize() { resize_requested_ = true; }
    // Safe during UI events: actual swapchain replacement is deferred to begin_frame.
    void set_vsync(bool enabled) {
        if (vsync_requested_ != enabled) {
            vsync_requested_ = enabled;
            request_resize();
        }
    }
    void wait_idle();
    void shutdown() noexcept;
    Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool readback = false);
    Buffer upload_buffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroy_buffer(Buffer buffer);
    // staged=true records into the active frame, before begin_rendering, without a queue wait.
    Texture create_texture(const unsigned char* pixels, int width, int height, bool nearest,
                           bool staged = false);
    void destroy_texture(Texture texture);
    void defer(std::function<void()> destroy);
    void immediate(const std::function<void(VkCommandBuffer)>& record);
    VkShaderModule shader(const std::filesystem::path& path);
    void memory_usage(uint64_t& used, uint64_t& allocated) const;

    VkInstance instance{};
    VkPhysicalDevice physical_device{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t queue_family{};
    VmaAllocator allocator{};
    VkDescriptorPool descriptor_pool{};
    VkDescriptorSetLayout texture_layout{};
    VkFormat colour_format{};
    VkExtent2D extent{};
    VkCommandBuffer command{};
    uint32_t image_count() const { return static_cast<uint32_t>(images_.size()); }
    uint32_t frame_slot() const { return frame_index_; }
    const std::string& gpu_name() const { return gpu_name_; }
    const char* present_mode_description() const;
    double gpu_ms() const { return gpu_ms_; }
    double upload_gpu_ms() const { return upload_gpu_ms_; }
    bool gpu_timing_supported() const { return timestamp_bits_ != 0; }
    std::atomic_uint validation_errors{0};
    std::atomic_uint validation_warnings{0};
    bool validation_enabled{};

  private:
    static constexpr uint32_t profile_capacity = 64;
    struct GpuProfile {
        uint64_t serial{};
        uint64_t start_tick{}, end_tick{};
        uint32_t count{}, width{}, height{}, columns{}, chunks{}, triangles{};
        bool steady{};
        std::array<const char*, profile_capacity> labels{};
        std::array<double, profile_capacity> milliseconds{};
    };
    struct UploadPage {
        Buffer buffer;
        VkDeviceSize capacity{}, used{};
    };
    struct UploadSlice {
        Buffer buffer;
        VkDeviceSize offset{};
    };
    struct Frame {
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkFence fence{};
        VkSemaphore acquired{};
        VkQueryPool queries{};
        VkQueryPool profile_queries{};
        GpuProfile profile;
        bool profile_pending{};
        uint64_t serial{};
        bool has_timestamps{};
        std::vector<UploadPage> uploads;
    };
    struct Deferred {
        uint64_t serial;
        std::function<void()> destroy;
    };
    std::array<Frame, frames_in_flight> frames_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VmaAllocation> offscreen_allocations_;
    // Presentation completion is tied to reacquiring each image, not a frame fence.
    std::vector<VkSemaphore> presented_;
    std::vector<Deferred> deferred_;
    SDL_Window* window_{};
    VkSurfaceKHR surface_{};
    VkSwapchainKHR swapchain_{};
    VkImage world_image_{};
    VkImageView world_view_{};
    bool rendering_started_{};
    VkPresentModeKHR present_mode_{VK_PRESENT_MODE_FIFO_KHR};
    VkDebugUtilsMessengerEXT messenger_{};
    VkCommandPool upload_pool_{};
    uint32_t frame_index_{}, image_index_{}, timestamp_bits_{};
    uint64_t submitted_serial_{}, completed_serial_{};
    VkDeviceSize upload_alignment_{};
    bool recording_{}, resize_requested_{}, vsync_requested_{};
    bool headless_{};
    float timestamp_period_{};
    double gpu_ms_{};
    double upload_gpu_ms_{};
    std::string gpu_name_;
    bool gpu_profile_enabled_{};
    bool frame_profile_enabled_{};
    CpuFrameTiming cpu_frame_timing_;
    std::vector<GpuProfile> gpu_profiles_;
    void collect_gpu_profile(Frame& frame);
    void create_swapchain();
    void create_offscreen_images(VkExtent2D size);
    void destroy_swapchain();
    void collect();
    UploadSlice stage_buffer(const void* data, VkDeviceSize size);
};
} // namespace sandbox
