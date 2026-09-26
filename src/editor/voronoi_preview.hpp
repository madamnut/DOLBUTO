#pragma once
#include <nlohmann/json_fwd.hpp>
#include <vector>
namespace sandbox::editor {
// Editor only, no persistence. Version3 Float32 response,16-float header:
// version,w,h,coarseCount,coarseSpacing,coarseSites,fineCount,fineSpacing,sampledLeaves,ms,
// coarseStride,leafStride,pixelStride,coarseOffset,leafOffset,pixelOffset.
// Coarse6: x,z,landNoise,region(0ocean/1continent/2archipelago),archNoise,reserved.
// Leaf6: globalFineId,x,z,islandNoise,edgeWeight(-1outside),isLand.
// Pixel4: parentId,leafIndex(-1none),normalizedF1,kind(0ocean/1continent/2island/3archSea).
std::vector<float> voronoi_preview(const nlohmann::ordered_json& request);
} // namespace sandbox::editor
