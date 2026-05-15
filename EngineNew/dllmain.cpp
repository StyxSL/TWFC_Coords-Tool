#include <Windows.h>
#include <TlHelp32.h>
#include <d3d9.h>
#include <fstream>
#include <chrono>
#include <ctime>
#include <string>
#include <atomic>
#include <vector>
#include <thread>

#pragma comment(lib, "d3d9.lib")

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::atomic<bool> bMenuOpen(false);
std::atomic<bool> bMenuThreadRunning(false);
HWND g_hWnd = NULL;
WNDCLASSEXW g_wc = {};

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
D3DPRESENT_PARAMETERS g_d3dpp = {};

struct SavedPosition {
    float x, y, z;
    std::string name;
};

std::vector<SavedPosition> g_savedPositions;
float g_currentX = 0, g_currentY = 0, g_currentZ = 0;
bool g_isInitialized = false;
CRITICAL_SECTION g_cs;

void WriteToLog(const std::string& message)
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    struct tm logTime;
    localtime_s(&logTime, &now_time);

    char path[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, path);
    std::string logFilePath = std::string(path) + "\\Debug-EngineNew.log";
    std::ofstream logFile(logFilePath, std::ios::app);

    if (logFile.is_open())
    {
        logFile << "[" << logTime.tm_hour << ":" << logTime.tm_min << ":" << logTime.tm_sec << "] " << message << std::endl;
        logFile.close();
    }
}

DWORD GetBaseAddress()
{
    return (DWORD)GetModuleHandleW(NULL);
}

bool ReadCoordinatesDirect(float& x, float& y, float& z)
{
    DWORD baseAddr = GetBaseAddress();
    if (baseAddr == 0) return false;

    DWORD staticAddr = baseAddr + 0x02210968;

    __try {
        DWORD ptr1 = *(DWORD*)staticAddr;
        if (ptr1 == 0 || ptr1 < 0x10000) return false;

        DWORD ptr2 = *(DWORD*)(ptr1 + 0x198);
        if (ptr2 == 0 || ptr2 < 0x10000) return false;

        DWORD ptr3 = *(DWORD*)(ptr2 + 0x34);
        if (ptr3 == 0 || ptr3 < 0x10000) return false;

        DWORD ptr4 = *(DWORD*)ptr3;
        if (ptr4 == 0 || ptr4 < 0x10000) return false;

        x = *(float*)(ptr4 + 0xD8);
        y = *(float*)(ptr4 + 0xDC);
        z = *(float*)(ptr4 + 0xE0);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteCoordinatesDirect(float x, float y, float z)
{
    DWORD baseAddr = GetBaseAddress();
    if (baseAddr == 0) return false;

    DWORD staticAddr = baseAddr + 0x02210968;

    __try {
        DWORD ptr1 = *(DWORD*)staticAddr;
        if (ptr1 == 0 || ptr1 < 0x10000) return false;

        DWORD ptr2 = *(DWORD*)(ptr1 + 0x198);
        if (ptr2 == 0 || ptr2 < 0x10000) return false;

        DWORD ptr3 = *(DWORD*)(ptr2 + 0x34);
        if (ptr3 == 0 || ptr3 < 0x10000) return false;

        DWORD ptr4 = *(DWORD*)ptr3;
        if (ptr4 == 0 || ptr4 < 0x10000) return false;

        *(float*)(ptr4 + 0xD8) = x;
        *(float*)(ptr4 + 0xDC) = y;
        *(float*)(ptr4 + 0xE0) = z;

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DWORD WINAPI UpdaterThread(LPVOID lpParam)
{
    WriteToLog("[INFO] Updater thread started");

    DWORD lastUpdate = 0;
    const int UPDATE_DELAY_MS = 33;

    while (bMenuOpen.load())
    {
        DWORD now = GetTickCount();

        if (now - lastUpdate >= UPDATE_DELAY_MS)
        {
            lastUpdate = now;

            float x, y, z;
            if (ReadCoordinatesDirect(x, y, z))
            {
                EnterCriticalSection(&g_cs);
                g_currentX = x;
                g_currentY = y;
                g_currentZ = z;
                g_isInitialized = true;
                LeaveCriticalSection(&g_cs);
            }
            else
            {
                EnterCriticalSection(&g_cs);
                g_isInitialized = false;
                LeaveCriticalSection(&g_cs);
            }
        }

        Sleep(10);
    }

    WriteToLog("[INFO] Updater thread stopped");
    return 0;
}

void RenderUI()
{
    if (!bMenuOpen.load()) return;

    float currentX, currentY, currentZ;
    bool isInit;

    EnterCriticalSection(&g_cs);
    currentX = g_currentX;
    currentY = g_currentY;
    currentZ = g_currentZ;
    isInit = g_isInitialized;
    LeaveCriticalSection(&g_cs);

    ImGui::SetNextWindowSize(ImVec2(450, 550), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("TWFC Coords", NULL, ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.00f), "Status");
        ImGui::Separator();

        if (isInit)
        {
            ImGui::TextColored(ImVec4(0.00f, 1.00f, 0.00f, 1.00f), "Connected");
        }
        else
        {
            ImGui::TextColored(ImVec4(1.00f, 0.50f, 0.00f, 1.00f), "Not connected");
            ImGui::Text("Start any level");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.00f), "Current Position");
        ImGui::Separator();

        if (isInit)
        {
            ImGui::Text("X: %.2f", currentX);
            ImGui::Text("Y: %.2f", currentY);
            ImGui::Text("Z: %.2f", currentZ);
        }
        else
        {
            ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.00f), "No data");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.00f), "Teleport");
        ImGui::Separator();

        static float tx = 0, ty = 0, tz = 0;
        ImGui::InputFloat("X", &tx);
        ImGui::InputFloat("Y", &ty);
        ImGui::InputFloat("Z", &tz);

        if (ImGui::Button("Teleport", ImVec2(-1, 30)) && isInit)
        {
            if (WriteCoordinatesDirect(tx, ty, tz))
            {
                WriteToLog("Teleported to: " + std::to_string(tx) + ", " + std::to_string(ty) + ", " + std::to_string(tz));
                EnterCriticalSection(&g_cs);
                g_currentX = tx;
                g_currentY = ty;
                g_currentZ = tz;
                LeaveCriticalSection(&g_cs);
            }
            else
            {
                WriteToLog("Teleport failed!");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.00f), "Saved Positions");
        ImGui::Separator();

        static char saveName[64] = "";
        ImGui::InputText("Name", saveName, sizeof(saveName));
        ImGui::SameLine();

        if (ImGui::Button("Save") && isInit && strlen(saveName) > 0)
        {
            SavedPosition pos;
            pos.x = currentX;
            pos.y = currentY;
            pos.z = currentZ;
            pos.name = saveName;
            g_savedPositions.push_back(pos);
            WriteToLog("Saved: " + std::string(saveName));
            memset(saveName, 0, sizeof(saveName));
        }

        ImGui::Spacing();

        for (size_t i = 0; i < g_savedPositions.size(); i++)
        {
            ImGui::PushID(i);

            if (ImGui::Button("TP", ImVec2(30, 0)))
            {
                if (WriteCoordinatesDirect(g_savedPositions[i].x,
                    g_savedPositions[i].y,
                    g_savedPositions[i].z))
                {
                    WriteToLog("TP to: " + g_savedPositions[i].name);
                    EnterCriticalSection(&g_cs);
                    g_currentX = g_savedPositions[i].x;
                    g_currentY = g_savedPositions[i].y;
                    g_currentZ = g_savedPositions[i].z;
                    LeaveCriticalSection(&g_cs);
                }
            }

            ImGui::SameLine();
            ImGui::Text("%s: (%.1f, %.1f, %.1f)",
                g_savedPositions[i].name.c_str(),
                g_savedPositions[i].x,
                g_savedPositions[i].y,
                g_savedPositions[i].z);

            ImGui::SameLine();

            if (ImGui::Button("X", ImVec2(20, 0)))
            {
                g_savedPositions.erase(g_savedPositions.begin() + i);
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::Text("Press F1 to close");
    }
    ImGui::End();
}

bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
        return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
    g_d3dpp.BackBufferWidth = 450;
    g_d3dpp.BackBufferHeight = 550;
    g_d3dpp.BackBufferCount = 1;
    g_d3dpp.EnableAutoDepthStencil = FALSE;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    g_d3dpp.hDeviceWindow = hWnd;

    HRESULT hr = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice);

    if (FAILED(hr))
    {
        WriteToLog("[ERROR] CreateDevice failed: 0x" + std::to_string(hr));
        return false;
    }

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = NULL; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

DWORD WINAPI MenuThread(LPVOID lpParam)
{
    WriteToLog("[INFO] Menu thread started");
    bMenuThreadRunning = true;

    Sleep(500);

    g_wc.cbSize = sizeof(WNDCLASSEXW);
    g_wc.style = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc = WndProc;
    g_wc.hInstance = GetModuleHandle(NULL);
    g_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    g_wc.hbrBackground = NULL;
    g_wc.lpszClassName = L"ImGuiWindowClass";

    RegisterClassExW(&g_wc);

    g_hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        L"ImGuiWindowClass",
        L"TWFC Trainer",
        WS_POPUP,
        100, 100, 450, 550,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!g_hWnd)
    {
        WriteToLog("[ERROR] Failed to create window");
        bMenuThreadRunning = false;
        return 1;
    }

    SetLayeredWindowAttributes(g_hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    if (!CreateDeviceD3D(g_hWnd))
    {
        WriteToLog("[ERROR] Failed to create D3D device");
        CleanupDeviceD3D();
        DestroyWindow(g_hWnd);
        bMenuThreadRunning = false;
        return 1;
    }

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableSetMousePos;

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.94f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.94f);

    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    CreateThread(NULL, 0, UpdaterThread, NULL, 0, NULL);

    WriteToLog("[INFO] Menu ready");

    MSG msg;
    while (bMenuOpen.load())
    {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderUI();

        ImGui::EndFrame();

        g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 255), 1.0f, 0);

        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }

        g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

        Sleep(10);
    }

    WriteToLog("[INFO] Menu thread exiting");

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hWnd);
    g_hWnd = NULL;

    bMenuThreadRunning = false;
    return 0;
}

DWORD WINAPI MainThread(HMODULE hModule)
{
    InitializeCriticalSection(&g_cs);

    WriteToLog("[INFO] Hooked successfully");

    bool bF1Pressed = false;

    while (true)
    {
        if (GetAsyncKeyState(VK_F1) & 0x8000)
        {
            if (!bF1Pressed)
            {
                bF1Pressed = true;

                if (!bMenuOpen.load())
                {
                    WriteToLog("[INFO] Opening menu...");
                    bMenuOpen = true;
                    CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
                }
                else
                {
                    WriteToLog("[INFO] Closing menu...");
                    bMenuOpen = false;
                    if (g_hWnd)
                        PostMessageW(g_hWnd, WM_QUIT, 0, 0);
                }
            }
        }
        else
        {
            bF1Pressed = false;
        }

        Sleep(50);
    }

    DeleteCriticalSection(&g_cs);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}