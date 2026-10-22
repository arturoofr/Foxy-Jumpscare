#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wchar.h>
#include <stdlib.h>
#include "resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

static const wchar_t* CLASS_NAME = L"FoxyTrayControllerClass";
static const UINT WM_TRAYICON = WM_USER + 1;
static const UINT_PTR TIMER_ID = 101;
static const unsigned int MIN_DENOMINATOR = 700;

static NOTIFYICONDATAW g_nid = {};
static HINSTANCE g_hInst = NULL;
static HWND g_hHiddenWnd = NULL;
static bool g_active = true;
static int g_volume = 100;
static unsigned int g_probDenominator = 10000;
static wchar_t g_configPath[MAX_PATH] = {};

void InitPaths();
void LoadSettings();
void SaveSettings();
bool IsRunOnStartup();
void SetRunOnStartup(bool enable);
void ShowTrayMenu(HWND hwnd);
void TriggerJumpscare();
INT_PTR CALLBACK VolumeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProbDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"FoxyJumpscare_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    g_hInst = hInstance;
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    InitPaths();
    LoadSettings();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = HiddenWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    g_hHiddenWnd = CreateWindowExW(0, CLASS_NAME, L"FoxyJumpscare", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hHiddenWnd) {
        CloseHandle(hMutex);
        return 0;
    }

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hHiddenWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Foxy Jumpscare");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    SetTimer(g_hHiddenWnd, TIMER_ID, 1000, NULL);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    CloseHandle(hMutex);
    return 0;
}

void InitPaths() {
    GetModuleFileNameW(NULL, g_configPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(g_configPath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    wcscat_s(g_configPath, MAX_PATH, L"config.ini");
}

void LoadSettings() {
    g_active = GetPrivateProfileIntW(L"Settings", L"Active", 1, g_configPath) != 0;
    g_volume = GetPrivateProfileIntW(L"Settings", L"Volume", 100, g_configPath);
    if (g_volume < 0) g_volume = 0;
    if (g_volume > 100) g_volume = 100;

    unsigned int denom = (unsigned int)GetPrivateProfileIntW(L"Settings", L"Denominator", 10000, g_configPath);
    if (denom < MIN_DENOMINATOR) denom = MIN_DENOMINATOR;
    g_probDenominator = denom;
}

void SaveSettings() {
    wchar_t buf[32];
    swprintf_s(buf, L"%d", g_active ? 1 : 0);
    WritePrivateProfileStringW(L"Settings", L"Active", buf, g_configPath);

    swprintf_s(buf, L"%d", g_volume);
    WritePrivateProfileStringW(L"Settings", L"Volume", buf, g_configPath);

    swprintf_s(buf, L"%u", g_probDenominator);
    WritePrivateProfileStringW(L"Settings", L"Denominator", buf, g_configPath);
}

bool IsRunOnStartup() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0, size = 0;
        bool exists = (RegQueryValueExW(hKey, L"FoxyJumpscare", NULL, &type, NULL, &size) == ERROR_SUCCESS);
        RegCloseKey(hKey);
        return exists;
    }
    return false;
}

void SetRunOnStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            wchar_t quotedPath[MAX_PATH + 4];
            swprintf_s(quotedPath, L"\"%s\"", exePath);
            RegSetValueExW(hKey, L"FoxyJumpscare", 0, REG_SZ, (const BYTE*)quotedPath, (DWORD)((wcslen(quotedPath) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"FoxyJumpscare");
        }
        RegCloseKey(hKey);
    }
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    HMENU hProbMenu = CreatePopupMenu();

    bool is1k = (g_probDenominator == 1000);
    bool is10k = (g_probDenominator == 10000);
    bool is100k = (g_probDenominator == 100000);
    bool is1m = (g_probDenominator == 1000000);
    bool isCustom = !(is1k || is10k || is100k || is1m);

    AppendMenuW(hProbMenu, MF_STRING | (is1k ? MF_CHECKED : 0), IDM_PROB_1000, L"1/1000");
    AppendMenuW(hProbMenu, MF_STRING | (is10k ? MF_CHECKED : 0), IDM_PROB_10000, L"1/10000");
    AppendMenuW(hProbMenu, MF_STRING | (is100k ? MF_CHECKED : 0), IDM_PROB_100000, L"1/100000");
    AppendMenuW(hProbMenu, MF_STRING | (is1m ? MF_CHECKED : 0), IDM_PROB_1000000, L"1/1000000");
    AppendMenuW(hProbMenu, MF_SEPARATOR, 0, NULL);

    wchar_t customLabel[64];
    if (isCustom) {
        swprintf_s(customLabel, L"Personalizada (1/%u)...", g_probDenominator);
    } else {
        wcscpy_s(customLabel, L"Personalizada...");
    }
    AppendMenuW(hProbMenu, MF_STRING | (isCustom ? MF_CHECKED : 0), IDM_PROB_CUSTOM, customLabel);

    wchar_t probMainLabel[64];
    swprintf_s(probMainLabel, L"Cambiar probabilidad (1/%u)", g_probDenominator);
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hProbMenu, probMainLabel);

    AppendMenuW(hMenu, MF_STRING | (g_active ? MF_CHECKED : 0), IDM_TOGGLE_ACTIVE, g_active ? L"Activado (Desactivar)" : L"Desactivado (Activar)");
    AppendMenuW(hMenu, MF_STRING | (IsRunOnStartup() ? MF_CHECKED : 0), IDM_STARTUP, L"Iniciar con Windows");

    wchar_t volLabel[64];
    swprintf_s(volLabel, L"Volumen (%d%%)...", g_volume);
    AppendMenuW(hMenu, MF_STRING, IDM_VOLUME, volLabel);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, L"Cerrar");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

INT_PTR CALLBACK VolumeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        HWND hSlider = GetDlgItem(hDlg, IDC_VOLUME_SLIDER);
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(hSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(hSlider, TBM_SETPOS, TRUE, g_volume);

        wchar_t buf[32];
        swprintf_s(buf, L"Volumen: %d%%", g_volume);
        SetDlgItemTextW(hDlg, IDC_VOLUME_TEXT, buf);
        return TRUE;
    }
    case WM_HSCROLL: {
        HWND hSlider = GetDlgItem(hDlg, IDC_VOLUME_SLIDER);
        if ((HWND)lParam == hSlider) {
            g_volume = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            wchar_t buf[32];
            swprintf_s(buf, L"Volumen: %d%%", g_volume);
            SetDlgItemTextW(hDlg, IDC_VOLUME_TEXT, buf);
            SaveSettings();
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK ProbDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG: {
        wchar_t buf[32];
        swprintf_s(buf, L"%u", g_probDenominator);
        SetDlgItemTextW(hDlg, IDC_PROB_EDIT, buf);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[64] = {};
            GetDlgItemTextW(hDlg, IDC_PROB_EDIT, buf, 63);
            const wchar_t* p = wcsrchr(buf, L'/');
            unsigned long val = wcstoul(p ? (p + 1) : buf, NULL, 10);
            if (val < MIN_DENOMINATOR) {
                MessageBoxW(hDlg, L"La probabilidad no puede ser mayor a 1/700 (el divisor debe ser como minimo 700).", L"Foxy Jumpscare", MB_ICONWARNING | MB_OK);
                SetDlgItemTextW(hDlg, IDC_PROB_EDIT, L"700");
                return TRUE;
            }
            g_probDenominator = (unsigned int)val;
            SaveSettings();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void TriggerJumpscare() {
}

LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        break;
    case WM_TIMER:
        if (wParam == TIMER_ID && g_active) {
            if ((unsigned int)(rand() % g_probDenominator) == 0) {
                TriggerJumpscare();
            }
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_PROB_1000:
            g_probDenominator = 1000;
            SaveSettings();
            break;
        case IDM_PROB_10000:
            g_probDenominator = 10000;
            SaveSettings();
            break;
        case IDM_PROB_100000:
            g_probDenominator = 100000;
            SaveSettings();
            break;
        case IDM_PROB_1000000:
            g_probDenominator = 1000000;
            SaveSettings();
            break;
        case IDM_PROB_CUSTOM:
            DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_PROB_DIALOG), hwnd, ProbDlgProc);
            break;
        case IDM_TOGGLE_ACTIVE:
            g_active = !g_active;
            SaveSettings();
            break;
        case IDM_STARTUP:
            SetRunOnStartup(!IsRunOnStartup());
            break;
        case IDM_VOLUME:
            DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_VOLUME_DIALOG), hwnd, VolumeDlgProc);
            break;
        case IDM_QUIT:
            PostQuitMessage(0);
            break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
