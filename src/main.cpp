#include "camera.hpp"
#include "dx_util.hpp"
#include "mesh_loader.hpp"
#include "renderer.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#pragma comment(lib, "comdlg32.lib")

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{

struct App
{
    meshviewer::Renderer *renderer = nullptr;
    meshviewer::OrbitCamera *camera = nullptr;
    meshviewer::MeshFit fit{};
    HWND hwnd = nullptr;
    bool dragging = false;
    int lastX = 0;
    int lastY = 0;
    enum class PendingCommand
    {
        None,
        Load,
        Close,
    };

    PendingCommand pendingCommand = PendingCommand::None;
    std::filesystem::path pendingMesh;
};

constexpr UINT kCommandOpen = 1;
constexpr UINT kCommandClose = 2;

std::wstring WidenUtf8(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int count =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0)
    {
        std::wstring fallback;
        fallback.reserve(text.size());
        for (unsigned char ch : text)
        {
            fallback.push_back(ch < 128 ? static_cast<wchar_t>(ch) : L'?');
        }
        return fallback;
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), count);
    return wide;
}

void ShowError(HWND hwnd, const std::wstring &message)
{
    MessageBoxW(hwnd, message.c_str(), L"Mesh Viewer", MB_OK | MB_ICONERROR);
}

std::filesystem::path ExecutableDirectory()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

DirectX::XMMATRIX WorldFromFit(const meshviewer::MeshFit &fit)
{
    using namespace DirectX;
    return XMMatrixTranslation(-fit.center[0], -fit.center[1], -fit.center[2]) *
           XMMatrixScaling(fit.scale, fit.scale, fit.scale);
}

void LoadInto(App &app, const std::filesystem::path &path)
{
    const meshviewer::CpuMesh mesh = meshviewer::LoadMesh(path);
    app.renderer->UploadMesh(mesh);
    app.fit = meshviewer::FitMesh(mesh);
    const std::wstring title = L"Mesh Viewer - " + path.filename().wstring();
    SetWindowTextW(app.hwnd, title.c_str());
}

void CloseMesh(App &app)
{
    if (!app.renderer->HasMesh())
    {
        return;
    }
    app.renderer->UnloadMesh();
    app.fit = {};
    SetWindowTextW(app.hwnd, L"Mesh Viewer");
}

bool PickMeshFile(HWND owner, std::filesystem::path &path)
{
    wchar_t buffer[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Mesh files (*.obj;*.fbx;*.gltf;*.glb)\0*.obj;*.fbx;*.gltf;*.glb\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Open Mesh";
    if (!GetOpenFileNameW(&dialog))
    {
        return false;
    }
    path = buffer;
    return true;
}

HMENU CreateMainMenu()
{
    HMENU file = CreatePopupMenu();
    HMENU bar = CreateMenu();
    if (!file || !bar)
    {
        if (file)
        {
            DestroyMenu(file);
        }
        if (bar)
        {
            DestroyMenu(bar);
        }
        return nullptr;
    }
    AppendMenuW(file, MF_STRING, kCommandOpen, L"Open\u2026");
    AppendMenuW(file, MF_STRING, kCommandClose, L"Close");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");
    return bar;
}

void ShowUsage()
{
    MessageBoxW(nullptr,
                L"meshviewer.exe [mesh-path]\n\n"
                L"Opens an obj, fbx, gltf, or glb file.\n"
                L"With no path, assets\\sample.obj next to the executable is loaded.\n"
                L"Drag a mesh file onto the window to replace it.\n"
                L"File, Open replaces the mesh. File, Close unloads it.\n\n"
                L"Left drag orbits. Mouse wheel zooms. Esc closes the window.",
                L"Mesh Viewer", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message)
    {
    case WM_LBUTTONDOWN:
        if (app)
        {
            app->dragging = true;
            app->lastX = GET_X_LPARAM(lParam);
            app->lastY = GET_Y_LPARAM(lParam);
            SetCapture(hwnd);
        }
        return 0;
    case WM_LBUTTONUP:
        if (app)
        {
            app->dragging = false;
        }
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (app)
        {
            app->dragging = false;
        }
        return 0;
    case WM_MOUSEMOVE:
        if (app && app->dragging && app->camera)
        {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            app->camera->AddOrbit(static_cast<float>(x - app->lastX), static_cast<float>(y - app->lastY));
            app->lastX = x;
            app->lastY = y;
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (app && app->camera)
        {
            app->camera->AddZoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)));
        }
        return 0;
    case WM_DROPFILES:
        if (app)
        {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t path[32768];
            if (DragQueryFileW(drop, 0, path, 32768) > 0)
            {
                app->pendingMesh = path;
                app->pendingCommand = App::PendingCommand::Load;
            }
            DragFinish(drop);
        }
        return 0;
    case WM_COMMAND:
        if (app)
        {
            switch (LOWORD(wParam))
            {
            case kCommandOpen: {
                std::filesystem::path path;
                if (PickMeshFile(hwnd, path))
                {
                    app->pendingMesh = std::move(path);
                    app->pendingCommand = App::PendingCommand::Load;
                }
                return 0;
            }
            case kCommandClose:
                app->pendingCommand = App::PendingCommand::Close;
                return 0;
            default:
                break;
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_SIZE:
        if (app && app->renderer && app->camera && wParam != SIZE_MINIMIZED)
        {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            try
            {
                app->renderer->Resize(width, height);
                app->camera->SetAspect(height == 0 ? 1.0f : static_cast<float>(width) / static_cast<float>(height));
            }
            catch (const std::exception &ex)
            {
                ShowError(hwnd, WidenUtf8(ex.what()));
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        ShowError(nullptr, L"CommandLineToArgvW failed");
        return 1;
    }

    std::filesystem::path requested;
    if (argc >= 2)
    {
        const std::wstring_view arg = argv[1];
        if (arg == L"-h" || arg == L"--help" || arg == L"/?")
        {
            LocalFree(argv);
            ShowUsage();
            return 0;
        }
        requested = argv[1];
    }
    LocalFree(argv);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.lpszClassName = L"MeshViewerWindow";
    if (!RegisterClassExW(&windowClass))
    {
        ShowError(nullptr, L"RegisterClassExW failed");
        return 1;
    }

    HMENU menu = CreateMainMenu();
    if (!menu)
    {
        ShowError(nullptr, L"CreateMenu failed");
        return 1;
    }

    RECT rect{0, 0, 1280, 720};
    const UINT dpi = GetDpiForSystem();
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, TRUE, 0, dpi);
    HWND hwnd =
        CreateWindowExW(0, windowClass.lpszClassName, L"Mesh Viewer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        rect.right - rect.left, rect.bottom - rect.top, nullptr, menu, instance, nullptr);
    if (!hwnd)
    {
        DestroyMenu(menu);
        ShowError(nullptr, L"CreateWindowExW failed");
        return 1;
    }

    meshviewer::Renderer renderer;
    meshviewer::OrbitCamera camera;
    App app;
    app.renderer = &renderer;
    app.camera = &camera;
    app.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    DragAcceptFiles(hwnd, TRUE);

    try
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const auto width = static_cast<std::uint32_t>(client.right - client.left);
        const auto height = static_cast<std::uint32_t>(client.bottom - client.top);
        camera.SetAspect(height == 0 ? 1.0f : static_cast<float>(width) / static_cast<float>(height));

        const std::filesystem::path exeDir = ExecutableDirectory();
        renderer.Initialize(hwnd, width, height, exeDir / "shaders" / "mesh.hlsl");
        const std::filesystem::path meshPath = requested.empty() ? exeDir / "assets" / "sample.obj" : requested;
        LoadInto(app, meshPath);
    }
    catch (const std::exception &ex)
    {
        ShowError(hwnd, WidenUtf8(ex.what()));
        return 1;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message{};
    while (message.message != WM_QUIT)
    {
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            continue;
        }
        if (IsIconic(hwnd))
        {
            WaitMessage();
            continue;
        }
        if (app.pendingCommand == App::PendingCommand::Load)
        {
            app.pendingCommand = App::PendingCommand::None;
            try
            {
                LoadInto(app, app.pendingMesh);
            }
            catch (const std::exception &ex)
            {
                ShowError(hwnd, WidenUtf8(ex.what()));
            }
        }
        else if (app.pendingCommand == App::PendingCommand::Close)
        {
            app.pendingCommand = App::PendingCommand::None;
            try
            {
                CloseMesh(app);
            }
            catch (const std::exception &ex)
            {
                ShowError(hwnd, WidenUtf8(ex.what()));
            }
        }
        try
        {
            renderer.Render(WorldFromFit(app.fit), camera.View(), camera.Projection());
        }
        catch (const std::exception &ex)
        {
            ShowError(hwnd, WidenUtf8(ex.what()));
            break;
        }
    }
    return 0;
}
