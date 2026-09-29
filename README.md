# DirectX 12 mesh viewer

Small Windows viewer for triangle meshes. It loads `.obj`, `.fbx`, `.gltf`, and `.glb` with Assimp, uploads one vertex buffer and one index buffer, and draws them with a directional light. Configure this repository from its root.

The DirectX 12 window cannot be compiled on a Linux host. On Windows it needs Visual Studio 2022, the Windows 10/11 SDK, and CMake.

## What it does

1. Creates an `IDXGIFactory4` (an `IDXGIFactory`), picks a hardware adapter, and creates an `ID3D12Device`. A WARP adapter is used only when no hardware adapter supports DirectX 12.
2. Creates a direct command queue, one command allocator, and a graphics command list.
3. Creates a double-buffered flip-model swap chain on a Win32 window, plus a depth buffer.
4. Builds a root signature with one constant buffer at `b0` (MVP, world matrix, light direction), a base-color texture at `t0`, and a static sampler at `s0`.
5. Compiles `shaders/mesh.hlsl` (`VSMain` / `PSMain`, shader model 5.0) with `D3DCompile` and binds both stages in a graphics pipeline state object.
6. Loads the mesh to vertex and index arrays, copies them from an upload heap into default-heap GPU buffers, and uploads one base-color texture the same way. A mesh with no texture uses a 1x1 white image.
7. Each frame builds a right-handed view and projection from the orbit camera, combines them with the model world matrix, and records clear, viewport, root signature, PSO, buffers, draw, `ExecuteCommandLists`, and `Present`.

Left drag orbits the mesh. The mouse wheel zooms. Esc closes the window. The mesh is centered and scaled so its longest side is 2 units.

`third_party/d3dx12.h` is Microsoft's D3DX12 helper header (MIT), the single-file form published with the [DirectX VS templates](https://github.com/walbourn/directx-vs-templates/blob/main/d3d12game_win32_dr/d3dx12.h). DirectXMath comes from the Windows SDK. Assimp comes from vcpkg.

## Architecture

`main.cpp` opens a Win32 window. The client area starts at 1280 by 720, adjusted for the system DPI. The window procedure turns left-drag into an orbit, the mouse wheel into a zoom, Esc into close, and a dropped file into a mesh reload. Resize rebuilds the swap-chain buffers and updates the camera aspect.

`Renderer` creates an `IDXGIFactory4`, skips software adapters, and creates an `ID3D12Device` at feature level 11.0. If no hardware adapter works, it uses the WARP software adapter. It then creates one direct command queue, one command allocator, and one graphics command list, plus a fence so the CPU can wait for the GPU. The swap chain is a two-buffer flip-discard chain (`DXGI_FORMAT_R8G8B8A8_UNORM`) on that window. A `D32_FLOAT` depth buffer matches the client size. Debug builds turn on the Direct3D debug layer when it is available.

A mesh file goes through Assimp. The import flags triangulate faces, join identical vertices, generate normals, improve cache locality, and sort by primitive type. The loader walks the node tree and bakes each node matrix into positions. Normals use the inverse-transpose of that matrix. Meshes that are not triangles are skipped, and faces that are not three indices are skipped. UV channel 0 is stored when the file has it, otherwise `(0, 0)`. Vertex color channel 0 is stored with alpha when the file has it, otherwise `(1, 1, 1, 1)`. Each CPU vertex is position, normal, UV, and color. The first material diffuse or base-color texture is kept: an embedded glTF, GLB, or FBX image, or an image file next to the mesh such as an OBJ `map_Kd`. Compressed images and files are decoded with WIC. Uncompressed embedded texels are copied as RGBA. The mesh is then centered and scaled so its longest side is 2 units. `UploadMesh` copies the vertex and index arrays through an upload heap into default-heap buffers, and uploads that one texture to a default-heap resource with a shader resource view. If the material has no texture, the resource is a 1x1 white texel.

Each frame the orbit camera builds a right-handed view aimed at the origin and a 45-degree perspective projection. The world matrix is the fit translation and scale. Those, with a fixed light direction, are written into one constant buffer at register `b0`. The command list clears the color and depth, sets the viewport, root signature, pipeline, and buffers, draws the indexed triangles, and presents.

The vertex shader transforms position by the model-view-projection matrix and the normal by the world matrix. It passes UV as `TEXCOORD0` and vertex color as `COLOR0`. The pixel shader samples the base-color texture at that UV, including alpha. It multiplies the sample by the directional-light term (a gray base color times `0.2 + 0.8 * NdotL`) and by the vertex color, then gamma-encodes the color for the UNORM swap chain. Output alpha is the texture alpha times the vertex-color alpha. The pipeline uses straight alpha blending (source alpha, inverse source alpha), so a transparent texel shows what was already in the render target. An opaque texture and the 1x1 white fallback have alpha 1, so that blend writes the same color as the previous opaque shading. A white texture and a white vertex color leave the shading unchanged. There is no skinning or animation.

`Mesh3DViewer.sln` and CMake both compile `src/main.cpp`, `src/camera.cpp`, `src/mesh_loader.cpp`, and `src/renderer.cpp`. CMake also builds the loader test. Both use C++20.

## Build on Windows

Install the **Desktop development with C++** workload and a current Windows SDK. vcpkg on this machine is `G:\GitHub\vcpkg`. From the repository root:

```powershell
$env:VCPKG_ROOT = "G:\GitHub\vcpkg"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
.\build\Release\meshviewer.exe
```

The first configure builds Assimp from `vcpkg.json`. That takes a while. vcpkg copies `assimp` DLLs next to the executable.

The Release executable is `build\Release\meshviewer.exe`. Shaders and `assets\sample.obj` are copied beside it.

## Visual Studio 2022

Open `Mesh3DViewer.sln` in this directory. The project uses the v143 toolset and x64. It installs Assimp from `vcpkg.json` when `VCPKG_ROOT` is set. On this machine that checkout is `G:\GitHub\vcpkg`.

Build **Release | x64**. The executable is `bin\x64\Release\meshviewer.exe`. The build copies `shaders\mesh.hlsl` and `assets\sample.obj` into `shaders\` and `assets\` next to that executable.

## Open a mesh

No path loads the sample sphere:

```powershell
.\build\Release\meshviewer.exe
```

Or pass a file. Relative paths are resolved from the current directory:

```powershell
.\build\Release\meshviewer.exe C:\models\crate.fbx
.\build\Release\meshviewer.exe .\scene.gltf
```

You can also drag a mesh file onto the window. `-h` shows the same usage text.

## Loader test

`meshviewer_loader_test` checks Assimp import, generated normals, outward winding, the sample mesh, and a node transform. It does not create a DirectX device. On Windows it builds with the viewer. On Linux, with Assimp installed (`libassimp-dev` or vcpkg):

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

Pass `-DCMAKE_CXX_COMPILER=g++` when the default `c++` is Clang and the link fails looking for `-lstdc++`.
