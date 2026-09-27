#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace meshviewer {

struct CpuVertex {
  float position[3];
  float normal[3];
};

static_assert(sizeof(CpuVertex) == 24, "GPU input layout packs position and normal tightly");

struct CpuMesh {
  std::vector<CpuVertex> vertices;
  std::vector<std::uint32_t> indices;
  float boundsMin[3]{};
  float boundsMax[3]{};
};

// Places the mesh at the origin and scales its longest axis to 2 units.
struct MeshFit {
  float center[3]{};
  float scale = 1.0f;
};

// Loads obj, fbx, or gltf/glb through Assimp into one triangle list.
[[nodiscard]] CpuMesh LoadMesh(const std::filesystem::path& path);

[[nodiscard]] CpuMesh LoadMeshFromMemory(const void* data, std::size_t size, const char* hint);

[[nodiscard]] MeshFit FitMesh(const CpuMesh& mesh);

}  // namespace meshviewer
