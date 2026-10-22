#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <wchar.h>
#include <stdlib.h>
#include "resource.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

using Microsoft::WRL::ComPtr;

static const wchar_t* CLASS_NAME = L"FoxyTrayControllerClass";
static const wchar_t* JUMPSCARE_CLASS = L"FoxyJumpscareOverlayClass";
static const UINT WM_TRAYICON = WM_USER + 1;
static const UINT_PTR TIMER_ID = 101;
static const unsigned int MIN_DENOMINATOR = 700;

static NOTIFYICONDATAW g_nid = {};
static HINSTANCE g_hInst = NULL;
static HWND g_hHiddenWnd = NULL;
static HWND g_hJumpscareWnd = NULL;
static bool g_active = true;
static bool g_isJumpscaring = false;
static int g_volume = 100;
static unsigned int g_probDenominator = 10000;

static wchar_t g_configPath[MAX_PATH] = {};
static wchar_t g_videoPath[MAX_PATH] = {};
static wchar_t g_iconPath[MAX_PATH] = {};

static ComPtr<IMFMediaEngine> g_pMediaEngine;
static ComPtr<ID3D11Device> g_pD3DDevice;
static ComPtr<ID3D11DeviceContext> g_pD3DContext;
static ComPtr<ID3D11Texture2D> g_pVideoTexture;
static UINT g_videoW = 0;
static UINT g_videoH = 0;
static HBITMAP g_hDIBSection = NULL;
static HDC g_hDIBDC = NULL;
static void* g_pDIBBits = NULL;

void InitPaths();
bool EnsureVideoFile();
void LoadSettings();
void SaveSettings();
bool IsRunOnStartup();
void SetRunOnStartup(bool enable);
void ShowTrayMenu(HWND hwnd);
HRESULT InitializeMediaEngine();
void TriggerJumpscare();
INT_PTR CALLBACK VolumeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ProbDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK JumpscareWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

class MediaEngineNotify : public IMFMediaEngineNotify {
    long m_cRef;
public:
    MediaEngineNotify() : m_cRef(1) {}
    virtual ~MediaEngineNotify() {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IMFMediaEngineNotify) || riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override {
        long cRef = InterlockedDecrement(&m_cRef);
        if (cRef == 0) delete this;
        return cRef;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override {
        if (event == MF_MEDIA_ENGINE_EVENT_CANPLAY) {
            if (g_pMediaEngine) g_pMediaEngine->Play();
        }
        if (event == MF_MEDIA_ENGINE_EVENT_ENDED || event == MF_MEDIA_ENGINE_EVENT_ERROR) {
            if (g_hJumpscareWnd) PostMessageW(g_hJumpscareWnd, WM_CLOSE, 0, 0);
        }
        return S_OK;
    }
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"FoxyJumpscare_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    g_hInst = hInstance;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icex);

    InitPaths();
    LoadSettings();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = HiddenWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    WNDCLASSW jwc = {};
    jwc.lpfnWndProc = JumpscareWindowProc;
    jwc.hInstance = hInstance;
    jwc.lpszClassName = JUMPSCARE_CLASS;
    RegisterClassW(&jwc);

    g_hHiddenWnd = CreateWindowExW(0, CLASS_NAME, L"FoxyJumpscare", 0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hHiddenWnd) {
        MFShutdown();
        CoUninitialize();
        CloseHandle(hMutex);
        return 0;
    }

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hHiddenWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!g_nid.hIcon) {
        g_nid.hIcon = (HICON)LoadImageW(NULL, g_iconPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED);
    }
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }
    wcscpy_s(g_nid.szTip, L"Foxy Jumpscare");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    SetTimer(g_hHiddenWnd, TIMER_ID, 1000, NULL);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    MFShutdown();
    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}

void InitPaths() {
    wchar_t baseDir[MAX_PATH] = {};
    GetModuleFileNameW(NULL, baseDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(baseDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    wcscpy_s(g_configPath, MAX_PATH, baseDir);
    wcscat_s(g_configPath, MAX_PATH, L"config.ini");

    wcscpy_s(g_videoPath, MAX_PATH, baseDir);
    wcscat_s(g_videoPath, MAX_PATH, L"foxyjumpscare.mp4");
    if (GetFileAttributesW(g_videoPath) == INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(g_videoPath, MAX_PATH, baseDir);
        wcscat_s(g_videoPath, MAX_PATH, L"assets\\foxyjumpscare.mp4");
    }

    wcscpy_s(g_iconPath, MAX_PATH, baseDir);
    wcscat_s(g_iconPath, MAX_PATH, L"foxyicon.ico");
    if (GetFileAttributesW(g_iconPath) == INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(g_iconPath, MAX_PATH, baseDir);
        wcscat_s(g_iconPath, MAX_PATH, L"assets\\foxyicon.ico");
    }
}

bool EnsureVideoFile() {
    if (GetFileAttributesW(g_videoPath) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) return false;
    wcscpy_s(g_videoPath, MAX_PATH, tempDir);
    wcscat_s(g_videoPath, MAX_PATH, L"foxyjumpscare.mp4");
    if (GetFileAttributesW(g_videoPath) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(IDR_MP4_VIDEO), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(g_hInst, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(g_hInst, hRes);
    const void* ptr = LockResource(hData);
    if (!ptr || size == 0) return false;

    HANDLE hFile = CreateFileW(g_videoPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(hFile, ptr, size, &written, NULL);
    CloseHandle(hFile);
    return written == size;
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
            if (g_pMediaEngine) g_pMediaEngine->SetVolume((double)g_volume / 100.0);
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

HRESULT InitializeMediaEngine() {
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    HRESULT hr = D3D11CreateDevice(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        &g_pD3DDevice,
        NULL,
        &g_pD3DContext
    );
    if (FAILED(hr)) return hr;

    ComPtr<ID3D10Multithread> pMultithread;
    if (SUCCEEDED(g_pD3DDevice.As(&pMultithread))) {
        pMultithread->SetMultithreadProtected(TRUE);
    }

    ComPtr<IMFDXGIDeviceManager> pManager;
    UINT resetToken = 0;
    hr = MFCreateDXGIDeviceManager(&resetToken, &pManager);
    if (FAILED(hr)) return hr;

    hr = pManager->ResetDevice(g_pD3DDevice.Get(), resetToken);
    if (FAILED(hr)) return hr;

    ComPtr<IMFMediaEngineClassFactory> pFactory;
    hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return hr;

    ComPtr<IMFAttributes> pAttr;
    hr = MFCreateAttributes(&pAttr, 3);
    if (FAILED(hr)) return hr;

    pAttr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, pManager.Get());

    MediaEngineNotify* pNotify = new MediaEngineNotify();
    pAttr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, pNotify);
    pNotify->Release();

    pAttr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    return pFactory->CreateInstance(0, pAttr.Get(), &g_pMediaEngine);
}

void TriggerJumpscare() {
    if (g_isJumpscaring) return;
    if (!EnsureVideoFile()) return;
    if (FAILED(InitializeMediaEngine())) return;

    g_isJumpscaring = true;
    g_pMediaEngine->SetVolume((double)g_volume / 100.0);

    BSTR bstrURL = SysAllocString(g_videoPath);
    g_pMediaEngine->SetSource(bstrURL);
    SysFreeString(bstrURL);

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_hJumpscareWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        JUMPSCARE_CLASS,
        L"",
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        NULL, NULL, g_hInst, NULL
    );
}

LRESULT CALLBACK JumpscareWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        SetLayeredWindowAttributes(hwnd, RGB(0, 255, 0), 0, LWA_COLORKEY);
        SetTimer(hwnd, 1, 16, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == 1 && g_pMediaEngine) {
            LONGLONG pts = 0;
            if (g_pMediaEngine->OnVideoStreamTick(&pts) == S_OK) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);

        bool frameDrawn = false;
        DWORD nativeW = 0, nativeH = 0;

        if (g_pMediaEngine && SUCCEEDED(g_pMediaEngine->GetNativeVideoSize(&nativeW, &nativeH)) && nativeW > 0) {
            DWORD width = nativeW;
            DWORD height = nativeH;

            if (!g_pVideoTexture || g_videoW != width || g_videoH != height) {
                g_pVideoTexture.Reset();
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = width;
                desc.Height = height;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
                g_pD3DDevice->CreateTexture2D(&desc, NULL, &g_pVideoTexture);
                g_videoW = width;
                g_videoH = height;

                if (g_hDIBDC) { DeleteDC(g_hDIBDC); g_hDIBDC = NULL; }
                if (g_hDIBSection) { DeleteObject(g_hDIBSection); g_hDIBSection = NULL; }

                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = (LONG)width;
                bmi.bmiHeader.biHeight = -((LONG)height);
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                HDC hDesktop = GetDC(NULL);
                g_hDIBDC = CreateCompatibleDC(hDesktop);
                g_hDIBSection = CreateDIBSection(hDesktop, &bmi, DIB_RGB_COLORS, &g_pDIBBits, NULL, 0);
                SelectObject(g_hDIBDC, g_hDIBSection);
                ReleaseDC(NULL, hDesktop);
            }

            if (g_pVideoTexture) {
                MFVideoNormalizedRect src = { 0.0f, 0.0f, 1.0f, 1.0f };
                RECT dest = { 0, 0, (LONG)width, (LONG)height };
                MFARGB bkgColor = { 0, 255, 0, 255 };

                if (SUCCEEDED(g_pMediaEngine->TransferVideoFrame(g_pVideoTexture.Get(), &src, &dest, &bkgColor))) {
                    ComPtr<IDXGISurface1> pSurface;
                    if (SUCCEEDED(g_pVideoTexture.As(&pSurface))) {
                        HDC hdcTexture = NULL;
                        if (SUCCEEDED(pSurface->GetDC(FALSE, &hdcTexture))) {
                            if (g_hDIBDC && g_pDIBBits) {
                                BitBlt(g_hDIBDC, 0, 0, (int)width, (int)height, hdcTexture, 0, 0, SRCCOPY);
                                DWORD* pPixel = (DWORD*)g_pDIBBits;
                                DWORD* pEnd = pPixel + (width * height);
                                while (pPixel < pEnd) {
                                    DWORD p = *pPixel;
                                    BYTE r = (BYTE)(p >> 16);
                                    BYTE g = (BYTE)(p >> 8);
                                    BYTE b = (BYTE)p;
                                    BYTE max_rb = (r > b) ? r : b;
                                    if (g > max_rb) {
                                        if (g - max_rb > 38) {
                                            *pPixel = 0x0000FF00;
                                        } else {
                                            *pPixel = (p & 0xFF0000FF) | ((DWORD)max_rb << 8);
                                        }
                                    }
                                    ++pPixel;
                                }
                                SetStretchBltMode(hdc, COLORONCOLOR);
                                StretchBlt(hdc, 0, 0, rect.right, rect.bottom, g_hDIBDC, 0, 0, (int)width, (int)height, SRCCOPY);
                                frameDrawn = true;
                            }
                            pSurface->ReleaseDC(NULL);
                        }
                    }
                }
            }
        }

        if (!frameDrawn) {
            HBRUSH hBrush = CreateSolidBrush(RGB(0, 255, 0));
            FillRect(hdc, &rect, hBrush);
            DeleteObject(hBrush);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_isJumpscaring = false;
        g_hJumpscareWnd = NULL;
        if (g_pMediaEngine) {
            g_pMediaEngine->Shutdown();
            g_pMediaEngine.Reset();
        }
        g_pVideoTexture.Reset();
        g_pD3DDevice.Reset();
        g_pD3DContext.Reset();
        if (g_hDIBDC) { DeleteDC(g_hDIBDC); g_hDIBDC = NULL; }
        if (g_hDIBSection) { DeleteObject(g_hDIBSection); g_hDIBSection = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK HiddenWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        break;
    case WM_TIMER:
        if (wParam == TIMER_ID && g_active && !g_isJumpscaring) {
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
