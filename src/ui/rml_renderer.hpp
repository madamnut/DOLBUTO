#pragma once
#include "render/renderer.hpp"
#include <RmlUi/Core/RenderInterface.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace sandbox {
class RmlRenderer final : public Rml::RenderInterface {
  public:
    explicit RmlRenderer(Renderer& renderer);
    ~RmlRenderer() override;
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override { scissor_enabled_ = enable; }
    void SetScissorRegion(Rml::Rectanglei region) override;
    void SetTransform(const Rml::Matrix4f* transform) override;
    uint32_t draw_calls{}, failed_textures{};

  private:
    struct Geometry {
        Buffer vertices, indices;
        uint32_t count;
    };
    Renderer& renderer_;
    VkPipelineLayout layout_{};
    VkPipeline pipeline_{};
    Texture white_{};
    std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometry_;
    std::unordered_map<Rml::TextureHandle, Texture> textures_;
    uintptr_t next_handle_{1};
    glm::mat4 transform_{1};
    bool scissor_enabled_{};
    int scissor_x_{}, scissor_y_{}, scissor_width_{}, scissor_height_{};
    Rml::TextureHandle texture(const unsigned char* pixels, int width, int height, bool nearest);
};
} // namespace sandbox
