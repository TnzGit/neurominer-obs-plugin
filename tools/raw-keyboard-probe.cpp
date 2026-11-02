// Standalone, on-demand Raw Input diagnostic for physical keyboard events.
// No game process access, injection, network, service, driver or privilege escalation.
// Only records while this program is running; Ctrl+C or close the console to stop.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::ofstream logFile;
HWND messageWindow = nullptr;
unsigned long long totalKeys = 0;

std::string describeKey(unsigned vkey)
{
    if (vkey >= 'A' && vkey <= 'Z') return std::string(1, static_cast<char>(vkey));
    if (vkey >= '0' && vkey <= '9') return std::string(1, static_cast<char>(vkey));
    switch (vkey) {
    case VK_LEFT: return "LeftArrow";
    case VK_RIGHT: return "RightArrow";
    case VK_UP: return "UpArrow";
    case VK_DOWN: return "DownArrow";
    case VK_SPACE: return "Space";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Control";
    case VK_MENU: return "Alt";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_TAB: return "Tab";
    default: return "VK" + std::to_string(vkey);
    }
}

void writeKeyboard(const RAWKEYBOARD &k, bool wasBackground)
{
    if (k.VKey == 255 || k.VKey == 0) return; // non-keyboard pseudo events
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    const auto vkey = static_cast<unsigned>(k.VKey);
    const auto scancode = static_cast<unsigned>(k.MakeCode);
    const bool isBreak = (k.Flags & RI_KEY_BREAK) != 0;
    const std::string key = describeKey(vkey);
    // All fields are numeric or from the fixed key mapping above, no user text.
    logFile << "{\"timestamp_us\":" << microseconds
            << ",\"type\":\"" << (isBreak ? "key_up" : "key_down") << "\""
            << ",\"source\":\"raw_input\""
            << ",\"key\":\"" << key << "\""
            << ",\"vkey\":" << vkey
            << ",\"scan_code\":" << scancode
            << ",\"raw_flags\":" << static_cast<unsigned>(k.Flags)
            << ",\"background\":" << (wasBackground ? "true" : "false")
            << "}\n";
    logFile.flush();
    ++totalKeys;
    std::cout << (isBreak ? "UP   " : "DOWN ") << key
              << " (scan=" << scancode << ", "
              << (wasBackground ? "background" : "foreground") << ")\n";
}

LRESULT CALLBACK inputWindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_INPUT) {
        const bool background = GET_RAWINPUT_CODE_WPARAM(wp) == RIM_INPUTSINK;
        UINT size = 0;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT,
                            nullptr, &size, sizeof(RAWINPUTHEADER)) == 0 &&
            size >= sizeof(RAWINPUTHEADER) && size < 65536) {
            std::vector<std::uint64_t> buffer((size + 7) / 8);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT,
                                buffer.data(), &size, sizeof(RAWINPUTHEADER)) == size) {
                const auto *raw = reinterpret_cast<const RAWINPUT *>(buffer.data());
                if (raw->header.dwType == RIM_TYPEKEYBOARD)
                    writeKeyboard(raw->data.keyboard, background);
            }
        }
        // Windows needs default cleanup for foreground RAWINPUT.
        if (!background) return DefWindowProcW(hwnd, message, wp, lp);
        return 0;
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
}

BOOL WINAPI stopHandler(DWORD event)
{
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
        event == CTRL_CLOSE_EVENT) {
        if (messageWindow) PostMessageW(messageWindow, WM_CLOSE, 0, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

int main()
{
    wchar_t tempPath[32768] = {};
    const DWORD n = GetTempPathW(32768, tempPath);
    if (n == 0 || n >= 32768) {
        std::cerr << "Cannot find Windows TEMP path\n";
        return 1;
    }
    const auto logPath = std::filesystem::path(tempPath) /
        (L"neurominer-raw-keyboard-probe-" +
         std::to_wstring(GetCurrentProcessId()) + L".jsonl");
    logFile.open(logPath, std::ios::out | std::ios::trunc);
    if (!logFile) {
        std::cerr << "Cannot open output log\n";
        return 2;
    }

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = inputWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"NeuroMinerStandaloneRawKeyboardProbe";
    if (!RegisterClassW(&wc)) {
        std::cerr << "RegisterClassW failed, error " << GetLastError() << "\n";
        return 3;
    }
    // Message-only window; never steals game focus.
    messageWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0,
                                   0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (!messageWindow) {
        std::cerr << "CreateWindowExW failed, error " << GetLastError() << "\n";
        return 4;
    }
    RAWINPUTDEVICE keyboard{};
    keyboard.usUsagePage = 0x01;
    keyboard.usUsage = 0x06;
    keyboard.dwFlags = RIDEV_INPUTSINK;
    keyboard.hwndTarget = messageWindow;
    if (!RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard))) {
        std::cerr << "RegisterRawInputDevices failed, error " << GetLastError() << "\n";
        DestroyWindow(messageWindow);
        return 5;
    }

    SetConsoleCtrlHandler(stopHandler, TRUE);
    std::wcout << L"Raw Input keyboard probe ACTIVE.\n"
               << L"Log: " << logPath.wstring() << L"\n"
               << L"Keep this console open. Focus the game and press movement/jump keys.\n"
               << L"Return here and press Ctrl+C to stop. DO NOT type passwords while active.\n";
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    std::cout << "Collected " << totalKeys << " raw keyboard messages.\n";
    logFile.flush();
    logFile.close();
    return 0;
}
