#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace meshviewer
{

struct CpuVertex
{
    float position[3];
    float normal[3];
    // xyz is the tangent. w is the bitangent handedness, +1 or -1. A zero xyz means the tangent is not usable.
    float tangent[4];
    float uv[2];
    float color[4];
};

static_assert(sizeof(CpuVertex) == 64, "GPU input layout packs position, normal, tangent, uv, and color tightly");
static_assert(offsetof(CpuVertex, normal) == 12, "normal follows position");
static_assert(offsetof(CpuVertex, tangent) == 24, "tangent follows normal");
static_assert(offsetof(CpuVertex, uv) == 40, "uv follows tangent");
static_assert(offsetof(CpuVertex, color) == 48, "color follows uv");

// One material image. Uncompressed RGBA, a compressed blob, or a file path. At most one is set.
struct CpuImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> encoded;
    std::filesystem::path file;
};

[[nodiscard]] inline bool HasImage(const CpuImage &image)
{
    return !image.rgba.empty() || !image.encoded.empty() || !image.file.empty();
}

struct CpuMesh
{
    std::vector<CpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    float boundsMin[3]{};
    float boundsMax[3]{};
    // First diffuse or base-color texture. Empty when the material has none.
    CpuImage baseColor;
    // First tangent-space normal texture (aiTextureType_NORMALS, including the glTF normalTexture slot).
    // A height, bump, or displacement map is not stored here.
    CpuImage normalMap;
};

// Places the mesh at the origin and scales its longest axis to 2 units.
struct MeshFit
{
    float center[3]{};
    float scale = 1.0f;
};

// Loads obj, fbx, or gltf/glb through Assimp into one triangle list.
[[nodiscard]] CpuMesh LoadMesh(const std::filesystem::path &path);

[[nodiscard]] CpuMesh LoadMeshFromMemory(const void *data, std::size_t size, const char *hint);

[[nodiscard]] MeshFit FitMesh(const CpuMesh &mesh);

} // namespace meshviewer
