# DirectX 12 mesh viewer

Small Windows viewer for triangle meshes. It loads `.obj`, `.fbx`, `.gltf`, and `.glb` with Assimp, uploads one vertex buffer and one index buffer, and draws them with a directional light. Configure this repository from its root.

The DirectX 12 window cannot be compiled on a Linux host. On Windows it needs Visual Studio 2022, the Windows 10/11 SDK, and CMake.

## What it does

1. Creates an `IDXGIFactory4` (an `IDXGIFactory`), picks a hardware adapter, and creates an `ID3D12Device`. A WARP adapter is used only when no hardware adapter supports DirectX 12.
2. Creates a direct command queue, one command allocator, and a graphics command list.
3. Creates a double-buffered flip-model swap chain on a Win32 window, plus a depth buffer.
4. Builds a root signature with one constant buffer at `b0` (MVP, world matrix, light direction). No textures are sampled.
5. Compiles `shaders/mesh.hlsl` (`VSMain` / `PSMain`, shader model 5.0) with `D3DCompile` and binds both stages in a graphics pipeline state object.
6. Loads the mesh to vertex and index arrays, copies them from an upload heap into default-heap GPU buffers, and fills a vertex buffer view and an index buffer view.
7. Each frame builds a right-handed view and projection from the orbit camera, combines them with the model world matrix, and records clear, viewport, root signature, PSO, buffers, draw, `ExecuteCommandLists`, and `Present`.

Left drag orbits the mesh. The mouse wheel zooms. Esc closes the window. The mesh is centered and scaled so its longest side is 2 units.

`third_party/d3dx12.h` is Microsoft's D3DX12 helper header (MIT), the single-file form published with the [DirectX VS templates](https://github.com/walbourn/directx-vs-templates/blob/main/d3d12game_win32_dr/d3dx12.h). DirectXMath comes from the Windows SDK. Assimp comes from vcpkg.

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
