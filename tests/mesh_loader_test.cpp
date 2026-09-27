#include "mesh_loader.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

int gFailures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
    ++gFailures;
  } else {
    std::cout << "ok  " << what << "\n";
  }
}

bool nearly(float value, float expected, float epsilon) {
  return std::fabs(value - expected) <= epsilon;
}

void expectValid(const meshviewer::CpuMesh& mesh, const char* what) {
  expect(!mesh.vertices.empty(), what);
  expect(mesh.indices.size() % 3 == 0, "index count is a multiple of 3");
  bool inRange = true;
  for (std::uint32_t index : mesh.indices) {
    if (index >= mesh.vertices.size()) {
      inRange = false;
      break;
    }
  }
  expect(inRange, "indices are in range");
}

bool facesPointOutward(const meshviewer::CpuMesh& mesh) {
  int checked = 0;
  for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
    const meshviewer::CpuVertex& a = mesh.vertices[mesh.indices[index]];
    const meshviewer::CpuVertex& b = mesh.vertices[mesh.indices[index + 1]];
    const meshviewer::CpuVertex& c = mesh.vertices[mesh.indices[index + 2]];
    const float ux = b.position[0] - a.position[0];
    const float uy = b.position[1] - a.position[1];
    const float uz = b.position[2] - a.position[2];
    const float vx = c.position[0] - a.position[0];
    const float vy = c.position[1] - a.position[1];
    const float vz = c.position[2] - a.position[2];
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;
    if (nx * nx + ny * ny + nz * nz < 1.0e-10f) {
      continue;
    }
    const float cx = (a.position[0] + b.position[0] + c.position[0]) / 3.0f;
    const float cy = (a.position[1] + b.position[1] + c.position[1]) / 3.0f;
    const float cz = (a.position[2] + b.position[2] + c.position[2]) / 3.0f;
    if (nx * cx + ny * cy + nz * cz <= 0.0f) {
      return false;
    }
    ++checked;
  }
  return checked > 0;
}

}  // namespace

int main() {
  constexpr std::string_view kTriangle = R"obj(
v 0 0 0
v 1 0 0
v 0 1 0
f 1 2 3
)obj";
  constexpr std::string_view kCube = R"obj(
v -1 -1 -1
v 1 -1 -1
v 1 1 -1
v -1 1 -1
v -1 -1 1
v 1 -1 1
v 1 1 1
v -1 1 1
f 1 4 3
f 1 3 2
f 5 6 7
f 5 7 8
f 1 5 8
f 1 8 4
f 2 3 7
f 2 7 6
f 1 2 6
f 1 6 5
f 4 8 7
f 4 7 3
)obj";
  // glTF node translation of +5 on X. The loader must bake the node matrix.
  constexpr std::string_view kTranslated =
      R"gltf({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"translation":[5.0,0.0,0.0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0.0,0.0,0.0],"max":[1.0,1.0,0.0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962},{"buffer":0,"byteOffset":36,"byteLength":6,"target":34963}],"buffers":[{"byteLength":44,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA="}]})gltf";

  try {
    const meshviewer::CpuMesh triangle =
        meshviewer::LoadMeshFromMemory(kTriangle.data(), kTriangle.size(), "obj");
    expectValid(triangle, "triangle has vertices");
    expect(triangle.vertices.size() == 3, "triangle keeps 3 vertices");
    expect(triangle.indices.size() == 3, "triangle keeps 1 face");
    expect(triangle.vertices[0].normal[2] > 0.9f, "generated triangle normal faces +Z");

    const meshviewer::CpuMesh cube = meshviewer::LoadMeshFromMemory(kCube.data(), kCube.size(), "obj");
    expectValid(cube, "cube has vertices");
    expect(cube.indices.size() == 36, "cube has 12 triangles");
    expect(facesPointOutward(cube), "cube triangles wind outward");

    const meshviewer::CpuMesh sample = meshviewer::LoadMesh(MESHVIEWER_SAMPLE_OBJ);
    expectValid(sample, "sample sphere has vertices");
    expect(sample.indices.size() >= 200 * 3, "sample sphere has a dense triangle list");
    expect(facesPointOutward(sample), "sample sphere triangles wind outward");
    const meshviewer::MeshFit fit = meshviewer::FitMesh(sample);
    expect(nearly(fit.scale, 1.0f, 1.0e-3f), "sample sphere fit scale is 1");
    expect(nearly(fit.center[0], 0.0f, 1.0e-3f) && nearly(fit.center[1], 0.0f, 1.0e-3f) &&
               nearly(fit.center[2], 0.0f, 1.0e-3f),
           "sample sphere is centered");

    const meshviewer::CpuMesh translated =
        meshviewer::LoadMeshFromMemory(kTranslated.data(), kTranslated.size(), "gltf");
    expectValid(translated, "translated gltf has vertices");
    if (!(translated.boundsMin[0] > 4.0f && translated.boundsMax[0] < 8.0f)) {
      std::cerr << "translated bounds min " << translated.boundsMin[0] << " " << translated.boundsMin[1] << " "
                << translated.boundsMin[2] << " max " << translated.boundsMax[0] << " " << translated.boundsMax[1]
                << " " << translated.boundsMax[2] << "\n";
    }
    expect(translated.boundsMin[0] > 4.0f && translated.boundsMax[0] < 8.0f, "gltf node translation is applied");

    bool missing = false;
    try {
      (void)meshviewer::LoadMesh("this-mesh-does-not-exist.obj");
    } catch (const std::runtime_error&) {
      missing = true;
    }
    expect(missing, "missing file throws");
  } catch (const std::exception& ex) {
    std::cerr << "FAIL: exception " << ex.what() << "\n";
    return 1;
  }

  if (gFailures != 0) {
    std::cerr << gFailures << " failure(s)\n";
    return 1;
  }
  std::cout << "mesh loader tests passed\n";
  return 0;
}
