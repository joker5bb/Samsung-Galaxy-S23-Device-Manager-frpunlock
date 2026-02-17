/*
 * Samsung Galaxy S23 Device Manager
 * Windows GUI Application using ADB/Fastboot
 * Compile with: g++ -o frpunlock.exe frpunlock.cpp -mwindows -lcomctl32 -lwininet -static-libgcc -static-libstdc++ -O2 -s
 * Requires: Windows SDK, MinGW-w64 or MSYS2
 */

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <iostream>
#include <cstring>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

// Application constants
#define APP_NAME "Samsung Galaxy S23 Device Manager"
#define APP_VERSION "2.0.0"
#define WM_UPDATE_LOG (WM_USER + 1)
#define WM_DEVICE_DETECTED (WM_USER + 2)

// Control IDs
#define IDC_BTN_DETECT 1001
#define IDC_BTN_ADB_SHELL 1002
#define IDC_BTN_RECOVERY 1003
#define IDC_BTN_DOWNLOAD 1004
#define IDC_BTN_BOOTLOADER 1005
#define IDC_BTN_UNLOCK_BL 1006
#define IDC_BTN_LOCK_BL 1007
#define IDC_BTN_FRP_BYPASS 1008
#define IDC_BTN_FLASH 1009
#define IDC_BTN_LOG_CLEAR 1010
#define IDC_LIST_DEVICES 1011
#define IDC_EDIT_LOG 1012
#define IDC_PROGRESS 1013
#define IDC_COMBO_COMMANDS 1014
#define IDC_BTN_EXECUTE 1015
#define IDC_CHK_AUTO_DETECT 1016

// Samsung Galaxy S23 Model IDs
struct DeviceModel {
    const char* model;
    const char* codename;
    const char* description;
};

DeviceModel s23_models[] = {
    {"SM-S911B", "dm1q", "Galaxy S23 (Global)"},
    {"SM-S911U", "dm1q", "Galaxy S23 (USA)"},
    {"SM-S911W", "dm1q", "Galaxy S23 (Canada)"},
    {"SM-S911N", "dm1q", "Galaxy S23 (Korea)"},
    {"SM-S916B", "dm2q", "Galaxy S23+ (Global)"},
    {"SM-S916U", "dm2q", "Galaxy S23+ (USA)"},
    {"SM-S918B", "dm3q", "Galaxy S23 Ultra (Global)"},
    {"SM-S918U", "dm3q", "Galaxy S23 Ultra (USA)"},
    {"SM-S918N", "dm3q", "Galaxy S23 Ultra (Korea)"},
    {nullptr, nullptr, nullptr}
};

// Global variables
HWND g_hWnd = NULL;
HWND g_hLog = NULL;
HWND g_hDeviceList = NULL;
HWND g_hProgress = NULL;
HWND g_hComboCmd = NULL;
std::atomic<bool> g_running(false);
std::mutex g_logMutex;
std::string g_adbPath;
std::string g_fastbootPath;

// Function prototypes
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL InitApplication(HINSTANCE);
BOOL InitInstance(HINSTANCE, int);
void AddLog(const std::string& msg);
void ClearLog();
std::string ExecuteCommand(const char* cmd, bool wait = true);
void DetectDevices();
void ExecuteADBCommand(const std::string& cmd);
void ExecuteFastbootCommand(const std::string& cmd);
void DrawGradient(HDC hdc, RECT* rect, COLORREF start, COLORREF end);

// Modern styling
#define COLOR_BG RGB(30, 30, 35)
#define COLOR_PANEL RGB(45, 45, 50)
#define COLOR_ACCENT RGB(0, 120, 215)
#define COLOR_TEXT RGB(240, 240, 240)
#define COLOR_SUCCESS RGB(0, 200, 100)
#define COLOR_WARNING RGB(255, 180, 0)
#define COLOR_ERROR RGB(255, 80, 80)

// Custom button class
class ModernButton {
public:
    HWND hwnd;
    COLORREF bgColor, hoverColor, textColor;
    bool isHover;
    
    ModernButton() : bgColor(COLOR_ACCENT), hoverColor(RGB(0, 140, 255)), 
                     textColor(COLOR_TEXT), isHover(false) {}
    
    void Create(HWND parent, int id, const char* text, int x, int y, int w, int h) {
        hwnd = CreateWindowEx(0, "BUTTON", text, 
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            x, y, w, h, parent, (HMENU)(INT_PTR)id, 
            GetModuleHandle(NULL), NULL);
        
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    }
    
    void Draw(LPDRAWITEMSTRUCT dis) {
        HDC hdc = dis->hDC;
        RECT rc = dis->rcItem;
        
        // Background
        COLORREF fill = (dis->itemState & ODS_SELECTED) ? 
            RGB(0, 100, 180) : (isHover ? hoverColor : bgColor);
        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        
        // Border
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 90, 160));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right - 1, rc.bottom - 1);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        
        // Text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        HFONT font = CreateFont(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, font);
        
        char text[256];
        GetWindowText(hwnd, text, 256);
        DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }
};

// Helper function to convert std::string to LPCSTR safely
inline LPCSTR str2lpcstr(const std::string& s) {
    return s.c_str();
}

// Main window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static ModernButton btnDetect, btnShell, btnRecovery, btnDownload;
    static ModernButton btnBootloader, btnUnlock, btnLock, btnFRP, btnFlash;
    
    switch (message) {
        case WM_CREATE: {
            // Initialize common controls
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
            InitCommonControlsEx(&icex);
            
            // Create fonts
            HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            
            HFONT hTitleFont = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            
            // Title label
            HWND hTitle = CreateWindow("STATIC", "Samsung Galaxy S23 Device Manager",
                WS_VISIBLE | WS_CHILD | SS_CENTER,
                0, 10, 900, 30, hWnd, NULL, NULL, NULL);
            SendMessage(hTitle, WM_SETFONT, (WPARAM)hTitleFont, TRUE);
            
            // Device list
            CreateWindow("STATIC", "Connected Devices:",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                20, 50, 200, 20, hWnd, NULL, NULL, NULL);
            
            g_hDeviceList = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL,
                WS_VISIBLE | WS_CHILD | LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS,
                20, 75, 400, 150, hWnd, (HMENU)IDC_LIST_DEVICES, NULL, NULL);
            SendMessage(g_hDeviceList, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Log window
            CreateWindow("STATIC", "Operation Log:",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                20, 235, 200, 20, hWnd, NULL, NULL, NULL);
            
            g_hLog = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", NULL,
                WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | 
                ES_READONLY | WS_VSCROLL,
                20, 260, 860, 250, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
            SendMessage(g_hLog, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Progress bar
            g_hProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL,
                WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
                20, 520, 860, 20, hWnd, (HMENU)IDC_PROGRESS, NULL, NULL);
            SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            
            // Command combo
            CreateWindow("STATIC", "Quick Commands:",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                440, 50, 150, 20, hWnd, NULL, NULL, NULL);
            
            g_hComboCmd = CreateWindow("COMBOBOX", NULL,
                WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                440, 75, 300, 200, hWnd, (HMENU)IDC_COMBO_COMMANDS, NULL, NULL);
            
            const char* commands[] = {
                "adb devices",
                "adb shell getprop ro.product.model",
                "adb shell getprop ro.build.version.release",
                "adb reboot bootloader",
                "adb reboot recovery",
                "fastboot devices",
                "fastboot oem device-info",
                "fastboot getvar all",
                "adb shell pm list packages",
                "adb logcat -d",
                nullptr
            };
            
            for (int i = 0; commands[i]; i++) {
                SendMessage(g_hComboCmd, CB_ADDSTRING, 0, (LPARAM)commands[i]);
            }
            SendMessage(g_hComboCmd, CB_SETCURSEL, 0, 0);
            
            // Buttons - Left column (Device Operations)
            int btnX = 440, btnY = 110, btnW = 200, btnH = 35, btnGap = 40;
            
            btnDetect.Create(hWnd, IDC_BTN_DETECT, "Detect Devices", btnX, btnY, btnW, btnH);
            btnShell.Create(hWnd, IDC_BTN_ADB_SHELL, "ADB Shell", btnX, btnY += btnGap, btnW, btnH);
            btnRecovery.Create(hWnd, IDC_BTN_RECOVERY, "Reboot Recovery", btnX, btnY += btnGap, btnW, btnH);
            btnDownload.Create(hWnd, IDC_BTN_DOWNLOAD, "Download Mode", btnX, btnY += btnGap, btnW, btnH);
            
            // Buttons - Right column (Advanced)
            btnX = 660; btnY = 110;
            btnBootloader.Create(hWnd, IDC_BTN_BOOTLOADER, "Bootloader", btnX, btnY, btnW, btnH);
            btnUnlock.Create(hWnd, IDC_BTN_UNLOCK_BL, "Unlock BL", btnX, btnY += btnGap, btnW, btnH);
            btnLock.Create(hWnd, IDC_BTN_LOCK_BL, "Lock BL", btnX, btnY += btnGap, btnW, btnH);
            btnFRP.Create(hWnd, IDC_BTN_FRP_BYPASS, "FRP Bypass", btnX, btnY += btnGap, btnW, btnH);
            
            // Execute and Flash buttons
            CreateWindow("BUTTON", "Execute Command",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                440, 270, 200, 35, hWnd, (HMENU)IDC_BTN_EXECUTE, NULL, NULL);
            
            btnFlash.Create(hWnd, IDC_BTN_FLASH, "Flash Firmware", 660, 270, 200, 35);
            
            // Clear log button
            CreateWindow("BUTTON", "Clear Log",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                760, 520, 120, 25, hWnd, (HMENU)IDC_BTN_LOG_CLEAR, NULL, NULL);
            
            // Auto-detect checkbox
            CreateWindow("BUTTON", "Auto-detect devices",
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                20, 550, 150, 20, hWnd, (HMENU)IDC_CHK_AUTO_DETECT, NULL, NULL);
            
            // Set default colors for buttons
            btnUnlock.bgColor = COLOR_WARNING;
            btnFRP.bgColor = RGB(200, 50, 50);
            btnFlash.bgColor = RGB(100, 50, 150);
            
            AddLog("Samsung Galaxy S23 Device Manager v" APP_VERSION " initialized");
            AddLog("Please ensure ADB drivers are installed and device is connected via USB");
            AddLog("For S23 series: Enable Developer Options > USB Debugging first");
            
            // Check for ADB
            WIN32_FIND_DATA findData;
            HANDLE hFind = FindFirstFile("adb.exe", &findData);
            if (hFind == INVALID_HANDLE_VALUE) {
                AddLog("WARNING: adb.exe not found in current directory!");
                AddLog("Please download Android SDK Platform Tools and place adb.exe here");
            } else {
                FindClose(hFind);
                g_adbPath = "adb.exe";
                g_fastbootPath = "fastboot.exe";
            }
            
            return 0;
        }
        
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            ModernButton* btn = (ModernButton*)GetWindowLongPtr(dis->hwndItem, GWLP_USERDATA);
            if (btn) {
                btn->Draw(dis);
                return TRUE;
            }
            break;
        }
        
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_DETECT:
                    DetectDevices();
                    break;
                    
                case IDC_BTN_ADB_SHELL: {
                    AddLog("Opening ADB Shell...");
                    ShellExecuteA(NULL, "open", "cmd.exe", 
                        "/k adb shell", NULL, SW_SHOW);
                    break;
                }
                
                case IDC_BTN_RECOVERY:
                    AddLog("Rebooting to Recovery mode...");
                    ExecuteADBCommand("reboot recovery");
                    break;
                    
                case IDC_BTN_DOWNLOAD:
                    AddLog("Rebooting to Download mode (Odin)...");
                    ExecuteADBCommand("reboot download");
                    break;
                    
                case IDC_BTN_BOOTLOADER:
                    AddLog("Rebooting to Bootloader/Fastboot mode...");
                    ExecuteADBCommand("reboot bootloader");
                    break;
                    
                case IDC_BTN_UNLOCK_BL: {
                    int result = MessageBoxA(hWnd, 
                        "WARNING: Unlocking bootloader will WIPE ALL DATA!\n\n"
                        "Samsung Knox will be tripped (permanent).\n"
                        "OEM Unlock must be enabled in Developer Options first.\n\n"
                        "Continue?", "Critical Warning", MB_YESNO | MB_ICONWARNING);
                    if (result == IDYES) {
                        AddLog("Initiating bootloader unlock sequence...");
                        ExecuteFastbootCommand("flashing unlock");
                    }
                    break;
                }
                
                case IDC_BTN_LOCK_BL: {
                    int result = MessageBoxA(hWnd, 
                        "WARNING: Locking bootloader will WIPE ALL DATA!\n\n"
                        "Continue?", "Critical Warning", MB_YESNO | MB_ICONWARNING);
                    if (result == IDYES) {
                        AddLog("Locking bootloader...");
                        ExecuteFastbootCommand("flashing lock");
                    }
                    break;
                }
                
                case IDC_BTN_FRP_BYPASS: {
                    int result = MessageBoxA(hWnd, 
                        "FRP Bypass Methods:\n\n"
                        "1. ADB Method (requires USB debugging enabled before reset)\n"
                        "2. Combination File Method (requires specific firmware)\n\n"
                        "Note: This is for legitimate device recovery only.\n"
                        "Proceed with ADB FRP bypass?", "FRP Bypass", MB_YESNO | MB_ICONQUESTION);
                    if (result == IDYES) {
                        AddLog("Attempting FRP bypass via ADB...");
                        // FRP bypass sequence for Samsung
                        ExecuteADBCommand("shell am start -n com.google.android.gsf.login/");
                        Sleep(1000);
                        ExecuteADBCommand("shell am start -n com.google.android.gsf.login.LoginActivity");
                        Sleep(1000);
                        ExecuteADBCommand("shell content insert --uri content://settings/secure --bind name:s:user_setup_complete --bind value:s:1");
                        AddLog("FRP bypass commands executed. Check device screen.");
                    }
                    break;
                }
                
                case IDC_BTN_FLASH: {
                    OPENFILENAMEA ofn;
                    char fileName[MAX_PATH] = "";
                    char params[MAX_PATH + 20] = "";
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = "Tar/MD5 Files\0*.tar;*.md5;*.tar.md5\0All Files\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    
                    if (GetOpenFileNameA(&ofn)) {
                        std::string fileStr = std::string(fileName);
                        AddLog("Selected firmware: " + fileStr);
                        AddLog("Use Odin3 to flash this firmware!");
                        
                        // Build parameter string safely
                        int ret = snprintf(params, sizeof(params), "/select,\"%s\"", fileName);
                        if (ret > 0 && ret < (int)sizeof(params)) {
                            ShellExecuteA(NULL, "open", "explorer.exe", 
                                params, NULL, SW_SHOW);
                        }
                    }
                    break;
                }
                
                case IDC_BTN_EXECUTE: {
                    int sel = (int)SendMessage(g_hComboCmd, CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR) {
                        char cmd[256];
                        SendMessage(g_hComboCmd, CB_GETLBTEXT, sel, (LPARAM)cmd);
                        AddLog("Executing: " + std::string(cmd));
                        std::string result = ExecuteCommand(cmd);
                        AddLog("Result:\n" + result);
                    }
                    break;
                }
                
                case IDC_BTN_LOG_CLEAR:
                    ClearLog();
                    break;
                    
                case IDC_LIST_DEVICES:
                    if (HIWORD(wParam) == LBN_DBLCLK) {
                        int sel = (int)SendMessage(g_hDeviceList, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR) {
                            char device[256];
                            SendMessage(g_hDeviceList, LB_GETTEXT, sel, (LPARAM)device);
                            AddLog("Selected device: " + std::string(device));
                        }
                    }
                    break;
            }
            break;
        }
        
        case WM_UPDATE_LOG: {
            std::string* msg = (std::string*)lParam;
            if (msg) {
                std::lock_guard<std::mutex> lock(g_logMutex);
                int len = GetWindowTextLength(g_hLog);
                SendMessage(g_hLog, EM_SETSEL, len, len);
                SendMessage(g_hLog, EM_REPLACESEL, 0, (LPARAM)(msg->c_str()));
                SendMessage(g_hLog, EM_REPLACESEL, 0, (LPARAM)("\r\n"));
                SendMessage(g_hLog, EM_SCROLLCARET, 0, 0);
                delete msg;
            }
            return 0;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            // Draw modern background
            RECT rect;
            GetClientRect(hWnd, &rect);
            DrawGradient(hdc, &rect, COLOR_BG, RGB(20, 20, 25));
            
            EndPaint(hWnd, &ps);
            break;
        }
        
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, COLOR_TEXT);
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Helper function to execute shell commands
std::string ExecuteCommand(const char* cmd, bool wait) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return "Error: Failed to create pipe";
    }
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWrite;
    si.hStdOutput = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    ZeroMemory(&pi, sizeof(pi));
    
    char cmdLine[1024];
    int ret = snprintf(cmdLine, sizeof(cmdLine), "cmd.exe /c %s", cmd);
    if (ret < 0 || ret >= (int)sizeof(cmdLine)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "Error: Command too long";
    }
    
    std::string result;
    if (CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        if (wait) {
            WaitForSingleObject(pi.hProcess, 10000); // 10 second timeout
            
            CloseHandle(hWrite);
            hWrite = NULL;
            
            char buffer[4096];
            DWORD bytesRead = 0;
            while (ReadFile(hRead, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                result += buffer;
            }
        } else {
            CloseHandle(hWrite);
            hWrite = NULL;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(hWrite);
        hWrite = NULL;
        result = "Error: Failed to execute command";
    }
    
    if (hRead) CloseHandle(hRead);
    if (hWrite) CloseHandle(hWrite);
    
    // Clean up result - convert single \n to \r\n for Windows edit control
    std::string cleaned;
    cleaned.reserve(result.size());
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == '\n' && (i == 0 || result[i-1] != '\r')) {
            cleaned += '\r';
            cleaned += '\n';
        } else {
            cleaned += result[i];
        }
    }
    
    return cleaned;
}

void AddLog(const std::string& msg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ", 
        st.wHour, st.wMinute, st.wSecond);
    
    std::string* fullMsg = new std::string(timestamp + msg);
    PostMessage(g_hWnd, WM_UPDATE_LOG, 0, (LPARAM)fullMsg);
}

void ClearLog() {
    SetWindowTextA(g_hLog, "");
    AddLog("Log cleared");
}

void DetectDevices() {
    AddLog("Scanning for devices...");
    SendMessage(g_hProgress, PBM_SETPOS, 10, 0);
    
    // Clear list
    SendMessage(g_hDeviceList, LB_RESETCONTENT, 0, 0);
    
    // Check ADB devices
    std::string adbResult = ExecuteCommand("adb devices -l");
    SendMessage(g_hProgress, PBM_SETPOS, 50, 0);
    
    std::istringstream adbStream(adbResult);
    std::string line;
    bool foundADB = false;
    
    while (std::getline(adbStream, line)) {
        if (line.find("device ") != std::string::npos && line.find("List of") == std::string::npos) {
            size_t pos = line.find("device ");
            if (pos != std::string::npos) {
                std::string device = "[ADB] " + line.substr(0, pos);
                SendMessageA(g_hDeviceList, LB_ADDSTRING, 0, (LPARAM)device.c_str());
                foundADB = true;
                
                // Get device info
                std::string serial = line.substr(0, pos);
                // Trim whitespace
                size_t start = serial.find_first_not_of(" \t\r\n");
                size_t end = serial.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    serial = serial.substr(start, end - start + 1);
                }
                
                if (!serial.empty()) {
                    std::string modelCmd = "adb -s " + serial + " shell getprop ro.product.model";
                    std::string model = ExecuteCommand(modelCmd.c_str());
                    // Trim newlines
                    model.erase(model.find_last_not_of("\r\n") + 1);
                    AddLog("Found device: " + model);
                    
                    // Check if it's S23 series
                    for (int i = 0; s23_models[i].model; i++) {
                        if (model.find(s23_models[i].model) != std::string::npos) {
                            AddLog("Samsung Galaxy S23 series detected: " + 
                                std::string(s23_models[i].description));
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // Check Fastboot devices
    std::string fbResult = ExecuteCommand("fastboot devices");
    SendMessage(g_hProgress, PBM_SETPOS, 80, 0);
    
    std::istringstream fbStream(fbResult);
    while (std::getline(fbStream, line)) {
        if (line.find("fastboot") != std::string::npos) {
            size_t pos = line.find("\t");
            if (pos != std::string::npos) {
                std::string device = "[FASTBOOT] " + line.substr(0, pos);
                SendMessageA(g_hDeviceList, LB_ADDSTRING, 0, (LPARAM)device.c_str());
                std::string deviceId = line.substr(0, pos);
                AddLog("Device in fastboot mode: " + deviceId);
            }
        }
    }
    
    SendMessage(g_hProgress, PBM_SETPOS, 100, 0);
    
    if (SendMessage(g_hDeviceList, LB_GETCOUNT, 0, 0) == 0) {
        AddLog("No devices found. Check USB connection and drivers.");
        SendMessageA(g_hDeviceList, LB_ADDSTRING, 0, (LPARAM)"No devices detected");
    } else {
        AddLog("Device scan complete");
    }
    
    Sleep(500);
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
}

void ExecuteADBCommand(const std::string& cmd) {
    if (g_adbPath.empty()) {
        AddLog("ERROR: ADB not found!");
        return;
    }
    std::string fullCmd = "adb " + cmd;
    std::string result = ExecuteCommand(fullCmd.c_str());
    if (!result.empty()) {
        AddLog(result);
    }
}

void ExecuteFastbootCommand(const std::string& cmd) {
    if (g_fastbootPath.empty()) {
        AddLog("ERROR: Fastboot not found!");
        return;
    }
    std::string fullCmd = "fastboot " + cmd;
    std::string result = ExecuteCommand(fullCmd.c_str());
    if (!result.empty()) {
        AddLog(result);
    }
}

void DrawGradient(HDC hdc, RECT* rect, COLORREF start, COLORREF end) {
    int r1 = GetRValue(start), g1 = GetGValue(start), b1 = GetBValue(start);
    int r2 = GetRValue(end), g2 = GetGValue(end), b2 = GetBValue(end);
    
    int height = rect->bottom - rect->top;
    if (height <= 0) return;
    
    for (int y = rect->top; y < rect->bottom; y++) {
        int r = r1 + (r2 - r1) * (y - rect->top) / height;
        int g = g1 + (g2 - g1) * (y - rect->top) / height;
        int b = b1 + (b2 - b1) * (y - rect->top) / height;
        
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, rect->left, y, NULL);
        LineTo(hdc, rect->right, y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
}

BOOL InitApplication(HINSTANCE hInstance) {
    WNDCLASSEXA wcex;
    wcex.cbSize = sizeof(WNDCLASSEXA);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "S23ManagerClass";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    return RegisterClassExA(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    g_hWnd = CreateWindowExA(0, "S23ManagerClass", APP_NAME " v" APP_VERSION,
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, 0, 920, 650, NULL, NULL, hInstance, NULL);
    
    if (!g_hWnd) return FALSE;
    
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    return TRUE;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
    LPSTR lpCmdLine, int nCmdShow) {
    
    (void)hPrevInstance;  // Suppress unused parameter warning
    (void)lpCmdLine;      // Suppress unused parameter warning
    
    // Enable visual styles
    InitCommonControls();
    
    if (!InitApplication(hInstance)) return FALSE;
    if (!InitInstance(hInstance, nCmdShow)) return FALSE;
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}