#include "renderer/RadialMenuRenderPath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace dolbuto
{
    namespace
    {
        constexpr std::size_t MaxRadialVertices = 16384;
        constexpr double Pi = 3.14159265358979323846;
        constexpr double TwoPi = Pi * 2.0;
        constexpr double StartAngle = -Pi * 0.5;
        constexpr int SegmentsPerCircle = 96;
        constexpr int MinimumSegmentsPerSector = 4;
        constexpr float DeadZoneRadius = 56.0f;
        constexpr float ActionOuterRadius = 132.0f;
        constexpr float CandidateOuterRadius = 210.0f;
        constexpr double SectorGapRadians = 0.012;
    }

    RadialMenuRenderPath::RadialMenuRenderPath(VkDevice* device, const VulkanResourceManager* gpuResources) :
        device_(device),
        gpuResources_(gpuResources)
    {
    }

    void RadialMenuRenderPath::createBuffers()
    {
        if (gpuResources_ == nullptr)
        {
            throw std::runtime_error("Radial menu render path GPU resources are not initialized.");
        }

        gpuResources_->createBuffer(
            sizeof(Vertex) * MaxRadialVertices,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBuffer_,
            vertexMemory_);
    }

    void RadialMenuRenderPath::destroy()
    {
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE)
        {
            return;
        }

        if (vertexBuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(*device_, vertexBuffer_, nullptr);
            vertexBuffer_ = VK_NULL_HANDLE;
        }
        if (vertexMemory_ != VK_NULL_HANDLE)
        {
            vkFreeMemory(*device_, vertexMemory_, nullptr);
            vertexMemory_ = VK_NULL_HANDLE;
        }
    }

    void RadialMenuRenderPath::draw(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        const Texture& whiteTexture,
        VkExtent2D extent,
        const game::RadialMenuRenderFrame& frame)
    {
        if (!frame.visible || frame.actionCount == 0 || extent.width == 0 || extent.height == 0)
        {
            return;
        }

        const float centerX = static_cast<float>(extent.width) * 0.5f;
        const float centerY = static_cast<float>(extent.height) * 0.5f;
        const double actionStep = TwoPi / static_cast<double>(frame.actionCount);

        std::vector<Vertex> vertices;
        std::vector<Vertex> allVertices;
        std::vector<DrawCommand> drawCommands;
        vertices.reserve(2048);
        allVertices.reserve(4096);

        const auto appendDraw = [&](SpriteRenderPath::Color color)
        {
            if (vertices.empty() || allVertices.size() + vertices.size() > MaxRadialVertices)
            {
                vertices.clear();
                return;
            }

            drawCommands.push_back(DrawCommand{
                static_cast<uint32_t>(allVertices.size()),
                static_cast<uint32_t>(vertices.size()),
                color
            });
            allVertices.insert(allVertices.end(), vertices.begin(), vertices.end());
            vertices.clear();
        };

        appendRingSector(vertices, extent, centerX, centerY, 0.0f, DeadZoneRadius, StartAngle, StartAngle + TwoPi);
        appendDraw({0.02f, 0.035f, 0.04f, 0.80f});

        for (uint32_t action = 0; action < frame.actionCount; ++action)
        {
            const double sectorStart = StartAngle + actionStep * static_cast<double>(action) + SectorGapRadians;
            const double sectorEnd = StartAngle + actionStep * static_cast<double>(action + 1u) - SectorGapRadians;
            appendRingSector(vertices, extent, centerX, centerY, DeadZoneRadius + 2.0f, ActionOuterRadius, sectorStart, sectorEnd);
            const bool selected = frame.selectedActionIndex == action;
            const SpriteRenderPath::Color color = selected
                ? SpriteRenderPath::Color{0.18f, 0.43f, 0.45f, 0.78f}
                : SpriteRenderPath::Color{0.035f, 0.075f, 0.085f, 0.58f};
            appendDraw(color);
        }

        if (frame.selectedActionIndex < frame.actionCount && frame.candidateCount > 0)
        {
            const double candidateStep = actionStep / static_cast<double>(frame.candidateCount);
            const double candidateStart = StartAngle + actionStep * static_cast<double>(frame.selectedActionIndex);
            for (uint32_t candidate = 0; candidate < frame.candidateCount; ++candidate)
            {
                const double sectorStart = candidateStart + candidateStep * static_cast<double>(candidate) + SectorGapRadians;
                const double sectorEnd = candidateStart + candidateStep * static_cast<double>(candidate + 1u) - SectorGapRadians;
                appendRingSector(vertices, extent, centerX, centerY, ActionOuterRadius + 2.0f, CandidateOuterRadius, sectorStart, sectorEnd);
                const bool selected = frame.selectedCandidateIndex == candidate;
                const bool enabled = candidate >= frame.candidateEnabled.size() || frame.candidateEnabled[candidate] != 0;
                const SpriteRenderPath::Color color = enabled
                    ? (selected
                        ? SpriteRenderPath::Color{0.26f, 0.56f, 0.50f, 0.82f}
                        : SpriteRenderPath::Color{0.04f, 0.075f, 0.07f, 0.58f})
                    : (selected
                        ? SpriteRenderPath::Color{0.54f, 0.16f, 0.13f, 0.82f}
                        : SpriteRenderPath::Color{0.22f, 0.035f, 0.035f, 0.62f});
                appendDraw(color);
            }
        }

        uploadVertices(allVertices);
        for (const DrawCommand& drawCommand : drawCommands)
        {
            drawUploadedVertices(commandBuffer, pipelineLayout, whiteTexture, drawCommand, drawCommand.color);
        }
    }

    void RadialMenuRenderPath::appendRingSector(
        std::vector<Vertex>& vertices,
        VkExtent2D extent,
        float centerX,
        float centerY,
        float innerRadius,
        float outerRadius,
        double startAngle,
        double endAngle) const
    {
        if (endAngle <= startAngle || outerRadius <= innerRadius)
        {
            return;
        }

        const int segmentCount = std::max(
            MinimumSegmentsPerSector,
            static_cast<int>(std::ceil((endAngle - startAngle) / TwoPi * static_cast<double>(SegmentsPerCircle))));
        for (int segment = 0; segment < segmentCount; ++segment)
        {
            const double t0 = static_cast<double>(segment) / static_cast<double>(segmentCount);
            const double t1 = static_cast<double>(segment + 1) / static_cast<double>(segmentCount);
            const double a0 = startAngle + (endAngle - startAngle) * t0;
            const double a1 = startAngle + (endAngle - startAngle) * t1;

            const float innerX0 = centerX + std::cos(a0) * innerRadius;
            const float innerY0 = centerY + std::sin(a0) * innerRadius;
            const float outerX0 = centerX + std::cos(a0) * outerRadius;
            const float outerY0 = centerY + std::sin(a0) * outerRadius;
            const float innerX1 = centerX + std::cos(a1) * innerRadius;
            const float innerY1 = centerY + std::sin(a1) * innerRadius;
            const float outerX1 = centerX + std::cos(a1) * outerRadius;
            const float outerY1 = centerY + std::sin(a1) * outerRadius;

            appendTriangle(vertices, extent, innerX0, innerY0, outerX0, outerY0, outerX1, outerY1);
            appendTriangle(vertices, extent, innerX0, innerY0, outerX1, outerY1, innerX1, innerY1);
        }
    }

    void RadialMenuRenderPath::appendTriangle(
        std::vector<Vertex>& vertices,
        VkExtent2D extent,
        float screenX0,
        float screenY0,
        float screenX1,
        float screenY1,
        float screenX2,
        float screenY2) const
    {
        vertices.push_back(vertexFromPixels(extent, screenX0, screenY0));
        vertices.push_back(vertexFromPixels(extent, screenX1, screenY1));
        vertices.push_back(vertexFromPixels(extent, screenX2, screenY2));
    }

    RadialMenuRenderPath::Vertex RadialMenuRenderPath::vertexFromPixels(VkExtent2D extent, float x, float y) const
    {
        return {
            x / static_cast<float>(extent.width) * 2.0f - 1.0f,
            y / static_cast<float>(extent.height) * 2.0f - 1.0f,
            0.5f,
            0.5f
        };
    }

    void RadialMenuRenderPath::uploadVertices(const std::vector<Vertex>& vertices) const
    {
        if (vertices.empty() || vertices.size() > MaxRadialVertices)
        {
            return;
        }
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE || vertexMemory_ == VK_NULL_HANDLE)
        {
            return;
        }

        const VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
        void* data = nullptr;
        vkMapMemory(*device_, vertexMemory_, 0, bufferSize, 0, &data);
        std::memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(*device_, vertexMemory_);
    }

    void RadialMenuRenderPath::drawUploadedVertices(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        const Texture& whiteTexture,
        const DrawCommand& drawCommand,
        SpriteRenderPath::Color color) const
    {
        if (drawCommand.vertexCount == 0)
        {
            return;
        }
        if (device_ == nullptr || *device_ == VK_NULL_HANDLE || vertexBuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        SpriteRenderPath::Push push{};
        push.data[0] = 0.0f;
        push.data[1] = 0.0f;
        push.data[2] = -1.0f;
        push.data[3] = 1.0f;
        push.data[4] = 0.0f;
        push.data[5] = 0.0f;
        push.data[6] = 1.0f;
        push.data[7] = 1.0f;
        push.data[8] = color.r;
        push.data[9] = color.g;
        push.data[10] = color.b;
        push.data[11] = color.a;

        const VkDeviceSize offset = sizeof(Vertex) * static_cast<VkDeviceSize>(drawCommand.firstVertex);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer_, &offset);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &whiteTexture.descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SpriteRenderPath::Push), &push);
        vkCmdDraw(commandBuffer, drawCommand.vertexCount, 1, 0, 0);
    }
}
