#define NOMINMAX

#include "VtrxpController.h"

#include <windows.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{

using VtrxpCommunicationLib::CommunicationState;
using VtrxpCommunicationLib::MockBackendOptions;
using VtrxpCommunicationLib::RegisterOperation;
using VtrxpCommunicationLib::RegisterOperationResult;
using VtrxpCommunicationLib::VtrxpController;

constexpr wchar_t WindowClassName[] = L"HgtdVtrxpLocalMockWindow";

bool saveWindowAsBitmap(HWND window, const wchar_t* path)
{
    RECT rectangle {};
    if (!GetWindowRect(window, &rectangle))
        return false;

    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    HDC windowContext = GetWindowDC(window);
    HDC memoryContext = CreateCompatibleDC(windowContext);
    HBITMAP bitmap = CreateCompatibleBitmap(windowContext, width, height);
    HGDIOBJ previousBitmap = SelectObject(memoryContext, bitmap);

    BOOL rendered = PrintWindow(window, memoryContext, 0x00000002);
    if (!rendered)
    {
        rendered = BitBlt(
            memoryContext,
            0,
            0,
            width,
            height,
            windowContext,
            0,
            0,
            SRCCOPY
        );
    }

    BITMAPINFO information {};
    information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    information.bmiHeader.biWidth = width;
    information.bmiHeader.biHeight = -height;
    information.bmiHeader.biPlanes = 1;
    information.bmiHeader.biBitCount = 32;
    information.bmiHeader.biCompression = BI_RGB;

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height)
            * 4
    );
    const int scanLines = GetDIBits(
        memoryContext,
        bitmap,
        0,
        static_cast<UINT>(height),
        pixels.data(),
        &information,
        DIB_RGB_COLORS
    );

    BITMAPFILEHEADER fileHeader {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits
        + static_cast<DWORD>(pixels.size());

    FILE* file = nullptr;
    const bool fileOpened = _wfopen_s(&file, path, L"wb") == 0 && file;
    bool written = false;
    if (fileOpened && rendered && scanLines == height)
    {
        written = std::fwrite(&fileHeader, sizeof(fileHeader), 1, file) == 1
            && std::fwrite(
                &information.bmiHeader,
                sizeof(information.bmiHeader),
                1,
                file
            ) == 1
            && std::fwrite(pixels.data(), pixels.size(), 1, file) == 1;
    }
    if (file)
        std::fclose(file);

    SelectObject(memoryContext, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryContext);
    ReleaseDC(window, windowContext);
    return written;
}

enum ControlId
{
    IdHeader = 10,
    IdMockBanner,
    IdCommunicationState,
    IdStart,
    IdStop,
    IdReset,
    IdAbout,
    IdAddress,
    IdValue,
    IdRead,
    IdWrite,
    IdLastReadAddress,
    IdLastReadValue,
    IdLastOperation,
    IdLastOperationSuccess,
    IdLastOperationStatus,
    IdLastError,
    IdGeneration,
    IdTemperature,
    IdSupply,
    IdRssi,
    IdTx1,
    IdTx2,
    IdTx3,
    IdTx4,
    IdEventLog
};

std::wstring widen(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

std::wstring formatAddress(std::uint32_t value)
{
    std::wostringstream stream;
    stream << L"0x" << std::uppercase << std::hex << value
           << std::dec << L"  (" << value << L")";
    return stream.str();
}

std::wstring formatByte(std::uint8_t value)
{
    std::wostringstream stream;
    stream << L"0x" << std::uppercase << std::hex
           << std::setw(2) << std::setfill(L'0')
           << static_cast<unsigned>(value)
           << std::dec << L"  (" << static_cast<unsigned>(value) << L")";
    return stream.str();
}

bool parseUnsigned(
    const std::wstring& original,
    std::uint64_t maximum,
    std::uint64_t& parsed,
    std::wstring& error
)
{
    const std::size_t first = original.find_first_not_of(L" \t\r\n");
    const std::size_t last = original.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
    {
        error = L"Input is empty";
        return false;
    }

    std::wstring text = original.substr(first, last - first + 1);
    int base = 10;
    if (text.size() > 2 && text[0] == L'0'
        && (text[1] == L'x' || text[1] == L'X'))
    {
        base = 16;
        text = text.substr(2);
    }
    if (text.empty())
    {
        error = L"No digits follow the hexadecimal prefix";
        return false;
    }

    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(text.c_str(), &end, base);
    if (errno == ERANGE || end == text.c_str() || *end != L'\0')
    {
        error = L"Enter a decimal value or a hexadecimal value beginning with 0x";
        return false;
    }
    if (value > maximum)
    {
        error = L"Input exceeds the allowed numeric range";
        return false;
    }

    parsed = static_cast<std::uint64_t>(value);
    error.clear();
    return true;
}

class LocalMockGui
{
public:
    explicit LocalMockGui(HINSTANCE instance):
        m_instance(instance)
    {
        resetController();
    }

    ~LocalMockGui()
    {
        if (m_controller)
            m_controller->stopCommunication();
        if (m_normalFont)
            DeleteObject(m_normalFont);
        if (m_boldFont)
            DeleteObject(m_boldFont);
        if (m_titleFont)
            DeleteObject(m_titleFont);
        if (m_headerBrush)
            DeleteObject(m_headerBrush);
        if (m_bannerBrush)
            DeleteObject(m_bannerBrush);
        if (m_runningBrush)
            DeleteObject(m_runningBrush);
        if (m_errorBrush)
            DeleteObject(m_errorBrush);
        if (m_neutralBrush)
            DeleteObject(m_neutralBrush);
        if (m_connectingBrush)
            DeleteObject(m_connectingBrush);
        if (m_monitorBrush)
            DeleteObject(m_monitorBrush);
    }

    bool create(int showCommand)
    {
        WNDCLASSW windowClass {};
        windowClass.lpfnWndProc = &LocalMockGui::windowProcedure;
        windowClass.hInstance = m_instance;
        windowClass.lpszClassName = WindowClassName;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);

        if (!RegisterClassW(&windowClass)
            && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        m_window = CreateWindowExW(
            0,
            WindowClassName,
            L"HGTD VTRx+ Control & Monitoring \u2014 LOCAL MOCK",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1215,
            780,
            nullptr,
            nullptr,
            m_instance,
            this
        );
        if (!m_window)
            return false;

        centerWindow();
        ShowWindow(
            m_window,
            showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand
        );
        UpdateWindow(m_window);
        return true;
    }

    void prepareLiveDemo()
    {
        handleReset();
        setText(
            IdEventLog,
            L"DEMO READY \u2014 click Connect / Start, WRITE 0x2A = 0x5A, READ 0x2A, then READ 0x100. No operation has been executed automatically."
        );
    }

    void loadSuccessfulPresentationState()
    {
        handleStart();
        handleWrite();
        handleRead();
    }

    void loadErrorPresentationState()
    {
        loadSuccessfulPresentationState();
        SetWindowTextW(GetDlgItem(m_window, IdAddress), L"0x100");
        handleRead();
    }

    bool captureSuccessfulPresentation(const wchar_t* path)
    {
        RedrawWindow(
            m_window,
            nullptr,
            nullptr,
            RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN
        );
        Sleep(200);
        const bool captured = saveWindowAsBitmap(m_window, path);
        PostMessageW(m_window, WM_CLOSE, 0, 0);
        return captured;
    }

    int run()
    {
        MSG message {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK windowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    )
    {
        LocalMockGui* application = nullptr;
        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            application = static_cast<LocalMockGui*>(create->lpCreateParams);
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(application)
            );
            application->m_window = window;
        }
        else
        {
            application = reinterpret_cast<LocalMockGui*>(
                GetWindowLongPtrW(window, GWLP_USERDATA)
            );
        }

        return application
            ? application->handleMessage(message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
            case WM_CREATE:
                createControls();
                refreshAll();
                return 0;

            case WM_COMMAND:
                if (HIWORD(wParam) == BN_CLICKED)
                {
                    switch (LOWORD(wParam))
                    {
                        case IdStart: handleStart(); return 0;
                        case IdStop: handleStop(); return 0;
                        case IdReset: handleReset(); return 0;
                        case IdAbout: showArchitecture(); return 0;
                        case IdRead: handleRead(); return 0;
                        case IdWrite: handleWrite(); return 0;
                    }
                }
                break;

            case WM_CTLCOLORSTATIC:
            {
                const HWND control = reinterpret_cast<HWND>(lParam);
                const int id = GetDlgCtrlID(control);
                HDC deviceContext = reinterpret_cast<HDC>(wParam);
                if (id == IdHeader)
                {
                    SetTextColor(deviceContext, RGB(255, 255, 255));
                    SetBkColor(deviceContext, RGB(20, 50, 90));
                    return reinterpret_cast<LRESULT>(m_headerBrush);
                }
                if (id == IdMockBanner)
                {
                    SetTextColor(deviceContext, RGB(120, 55, 0));
                    SetBkColor(deviceContext, RGB(255, 240, 190));
                    return reinterpret_cast<LRESULT>(m_bannerBrush);
                }
                if (id == IdCommunicationState)
                {
                    const CommunicationState state =
                        m_controller->communicationState();
                    HBRUSH brush = m_neutralBrush;
                    COLORREF foreground = RGB(55, 65, 75);
                    COLORREF background = RGB(237, 241, 245);
                    if (state == CommunicationState::Running)
                    {
                        brush = m_runningBrush;
                        foreground = RGB(20, 95, 45);
                        background = RGB(218, 244, 226);
                    }
                    else if (state == CommunicationState::Error)
                    {
                        brush = m_errorBrush;
                        foreground = RGB(150, 25, 35);
                        background = RGB(255, 225, 225);
                    }
                    else if (state == CommunicationState::Connecting)
                    {
                        brush = m_connectingBrush;
                        foreground = RGB(120, 75, 0);
                        background = RGB(255, 242, 204);
                    }
                    SetTextColor(deviceContext, foreground);
                    SetBkColor(deviceContext, background);
                    return reinterpret_cast<LRESULT>(brush);
                }
                if (id == IdLastOperationSuccess
                    || id == IdLastOperationStatus
                    || id == IdLastError)
                {
                    if (m_operationVisual > 0)
                    {
                        SetTextColor(deviceContext, RGB(20, 95, 45));
                        SetBkColor(deviceContext, RGB(218, 244, 226));
                        return reinterpret_cast<LRESULT>(m_runningBrush);
                    }
                    if (m_operationVisual < 0)
                    {
                        SetTextColor(deviceContext, RGB(150, 25, 35));
                        SetBkColor(deviceContext, RGB(255, 225, 225));
                        return reinterpret_cast<LRESULT>(m_errorBrush);
                    }
                }
                if (id == IdTemperature || id == IdSupply || id == IdRssi
                    || id == IdTx1 || id == IdTx2 || id == IdTx3 || id == IdTx4)
                {
                    SetTextColor(deviceContext, RGB(25, 70, 105));
                    SetBkColor(deviceContext, RGB(226, 240, 249));
                    return reinterpret_cast<LRESULT>(m_monitorBrush);
                }
                SetBkMode(deviceContext, TRANSPARENT);
                break;
            }

            case WM_CLOSE:
                if (m_controller)
                    m_controller->stopCommunication();
                DestroyWindow(m_window);
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(m_window, message, wParam, lParam);
    }

    void createControls()
    {
        HDC screen = GetDC(nullptr);
        const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
        ReleaseDC(nullptr, screen);
        m_normalFont = CreateFontW(
            -MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
        );
        m_boldFont = CreateFontW(
            -MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
        );
        m_titleFont = CreateFontW(
            -MulDiv(19, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
        );
        m_headerBrush = CreateSolidBrush(RGB(20, 50, 90));
        m_bannerBrush = CreateSolidBrush(RGB(255, 240, 190));
        m_runningBrush = CreateSolidBrush(RGB(218, 244, 226));
        m_errorBrush = CreateSolidBrush(RGB(255, 225, 225));
        m_neutralBrush = CreateSolidBrush(RGB(237, 241, 245));
        m_connectingBrush = CreateSolidBrush(RGB(255, 242, 204));
        m_monitorBrush = CreateSolidBrush(RGB(226, 240, 249));

        createStatic(
            L"HGTD VTRx+ Control & Monitoring \u2014 LOCAL MOCK",
            0, 0, 1200, 68, IdHeader,
            SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
            m_titleFont
        );
        createStatic(
            L"SIMULATION ONLY \u2014 values are MOCK data; no CERN, FELIX or detector hardware is connected",
            20, 78, 1160, 35, IdMockBanner, SS_CENTER | SS_CENTERIMAGE,
            m_boldFont
        );

        createGroup(L"Connection", 20, 125, 1160, 88);
        createLabel(L"Communication State", 40, 157, 180, 24);
        createReadOnly(IdCommunicationState, 225, 153, 250, 28);
        createButton(L"About / Architecture", IdAbout, 530, 148, 185, 35);
        createButton(L"Connect / Start", IdStart, 735, 148, 135, 35);
        createButton(L"Stop", IdStop, 890, 148, 105, 35);
        createButton(L"Reset", IdReset, 1015, 148, 105, 35);

        createGroup(L"Register Control", 20, 225, 440, 205);
        createLabel(L"Register Address", 42, 263, 130, 24);
        createEdit(IdAddress, L"0x2A", 180, 258, 220, 29);
        createLabel(L"Value", 42, 308, 130, 24);
        createEdit(IdValue, L"0x5A", 180, 303, 220, 29);
        createButton(L"READ", IdRead, 88, 350, 125, 38);
        createButton(L"WRITE", IdWrite, 240, 350, 125, 38);
        createStatic(
            L"Accepted input: decimal or 0x-prefixed hexadecimal",
            42, 397, 370, 20, 0, SS_LEFT, m_normalFont
        );

        createGroup(L"Diagnostics", 480, 225, 700, 300);
        int row = 254;
        createDiagnosticRow(L"Last Read Address", IdLastReadAddress, row); row += 36;
        createDiagnosticRow(L"Last Read Value", IdLastReadValue, row); row += 36;
        createDiagnosticRow(L"Last Operation", IdLastOperation, row); row += 36;
        createDiagnosticRow(L"Success", IdLastOperationSuccess, row); row += 36;
        createDiagnosticRow(L"Status", IdLastOperationStatus, row); row += 36;
        createDiagnosticRow(L"Last Error", IdLastError, row); row += 36;
        createDiagnosticRow(L"Generation", IdGeneration, row);

        createGroup(L"MOCK Monitoring - SIMULATED VALUES", 20, 445, 440, 230);
        createMonitoringRow(L"Temperature", IdTemperature, 474);
        createMonitoringRow(L"Supply Voltage", IdSupply, 501);
        createMonitoringRow(L"RSSI", IdRssi, 528);
        createMonitoringRow(L"TX1", IdTx1, 555);
        createMonitoringRow(L"TX2", IdTx2, 582);
        createMonitoringRow(L"TX3", IdTx3, 609);
        createMonitoringRow(L"TX4", IdTx4, 636);

        createGroup(L"Presentation Status", 480, 540, 700, 135);
        createReadOnly(IdEventLog, 500, 569, 655, 82, true);
        createStatic(
            L"LOCAL MOCK frontend | No OPC UA endpoint | No CERN hardware | All monitoring values are SIMULATED",
            20, 697, 1160, 24, 0, SS_CENTER, m_boldFont
        );
    }

    void createDiagnosticRow(const wchar_t* label, int id, int y)
    {
        createLabel(label, 500, y + 4, 165, 22);
        createReadOnly(id, 670, y, 485, 27);
    }

    void createMonitoringRow(const wchar_t* label, int id, int y)
    {
        createLabel(label, 42, y + 3, 135, 22);
        createReadOnly(id, 180, y, 255, 25);
    }

    HWND createControl(
        DWORD extendedStyle,
        const wchar_t* className,
        const wchar_t* text,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        int id,
        HFONT font
    )
    {
        HWND control = CreateWindowExW(
            extendedStyle,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            x, y, width, height,
            m_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            m_instance,
            nullptr
        );
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    }

    HWND createStatic(
        const wchar_t* text,
        int x,
        int y,
        int width,
        int height,
        int id,
        DWORD style,
        HFONT font
    )
    {
        return createControl(
            0, L"STATIC", text, style,
            x, y, width, height, id, font
        );
    }

    void createLabel(
        const wchar_t* text,
        int x,
        int y,
        int width,
        int height
    )
    {
        createStatic(text, x, y, width, height, 0, SS_LEFT, m_normalFont);
    }

    void createGroup(
        const wchar_t* text,
        int x,
        int y,
        int width,
        int height
    )
    {
        createControl(
            0, L"BUTTON", text, BS_GROUPBOX,
            x, y, width, height, 0, m_boldFont
        );
    }

    void createButton(
        const wchar_t* text,
        int id,
        int x,
        int y,
        int width,
        int height
    )
    {
        createControl(
            0, L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP,
            x, y, width, height, id, m_boldFont
        );
    }

    void createEdit(
        int id,
        const wchar_t* text,
        int x,
        int y,
        int width,
        int height
    )
    {
        createControl(
            WS_EX_CLIENTEDGE, L"EDIT", text,
            ES_AUTOHSCROLL | WS_TABSTOP,
            x, y, width, height, id, m_normalFont
        );
    }

    void createReadOnly(
        int id,
        int x,
        int y,
        int width,
        int height,
        bool multiline = false
    )
    {
        DWORD style = ES_READONLY | ES_AUTOHSCROLL;
        if (multiline)
            style = ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
        createControl(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            style,
            x, y, width, height, id, m_normalFont
        );
    }

    void setText(int id, const std::wstring& value)
    {
        SetWindowTextW(GetDlgItem(m_window, id), value.c_str());
    }

    std::wstring getText(int id) const
    {
        const HWND control = GetDlgItem(m_window, id);
        const int length = GetWindowTextLengthW(control);
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        if (length > 0)
            GetWindowTextW(control, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length));
        return text;
    }

    void resetController()
    {
        if (m_controller)
            m_controller->stopCommunication();
        m_controller = std::make_unique<VtrxpController>();
        MockBackendOptions options;
        options.connectionAvailable = true;
        options.responseDelay = std::chrono::milliseconds(20);
        m_controller->setMockOptions(options);
    }

    void showArchitecture()
    {
        MessageBoxW(
            m_window,
            L"LOCAL DEMO PATH (running now)\r\n"
            L"GUI  ->  VtrxpController  ->  IVtrxpBackend\r\n"
            L"     ->  MockVtrxpCommunicator\r\n"
            L"     ->  simulated 256-byte register bank\r\n\r\n"
            L"PRODUCTION CONCEPT (architectural context only)\r\n"
            L"WinCC OA  ->  OPC UA / QUASAR  ->  DVTRxp\r\n"
            L"     ->  VtrxpController  ->  REAL VtrxpCommunicator\r\n"
            L"     ->  FELIX  ->  lpGBT  ->  I2C  ->  VTRx+\r\n\r\n"
            L"The production path is NOT running locally.\r\n"
            L"All monitoring values in this GUI are SIMULATED / MOCK.",
            L"Architecture — HGTD VTRx+ LOCAL MOCK",
            MB_OK | MB_ICONINFORMATION
        );
    }

    void handleStart()
    {
        if (m_controller->communicationState() == CommunicationState::Running)
        {
            setText(IdEventLog, L"MOCK communication is already Running. Repeated start was handled safely.");
            return;
        }

        if (!m_controller->setupVtrxp("mock://local-gui", 0, 0, 0, 0, 0))
        {
            refreshConnectionState();
            setText(
                IdEventLog,
                L"MOCK setup failed: " + widen(m_controller->lastLifecycleError())
            );
            return;
        }
        refreshConnectionState();
        UpdateWindow(m_window);

        if (!m_controller->startCommunication())
        {
            refreshConnectionState();
            setText(
                IdEventLog,
                L"MOCK start failed: " + widen(m_controller->lastLifecycleError())
            );
            return;
        }

        refreshConnectionState();
        updateMonitoring();
        setText(
            IdEventLog,
            L"LOCAL MOCK communication started. Register operations are now available."
        );
    }

    void handleStop()
    {
        m_controller->stopCommunication();
        refreshConnectionState();
        updateMonitoring();
        setText(
            IdEventLog,
            L"MOCK communication stopped cleanly. Telemetry is frozen and operations will fail explicitly."
        );
    }

    void handleReset()
    {
        resetController();
        m_monitoringStep = 0;
        clearDiagnostics();
        refreshAll();
        SetWindowTextW(GetDlgItem(m_window, IdAddress), L"0x2A");
        SetWindowTextW(GetDlgItem(m_window, IdValue), L"0x5A");
        setText(
            IdEventLog,
            L"Application reset. Click Connect / Start to begin the deterministic LOCAL MOCK demonstration."
        );
    }

    void handleRead()
    {
        std::uint64_t parsed = 0;
        std::wstring error;
        if (!parseUnsigned(
                getText(IdAddress),
                std::numeric_limits<std::uint32_t>::max(),
                parsed,
                error
            ))
        {
            showInputError(L"Register address: " + error);
            return;
        }

        const RegisterOperationResult result = m_controller->registerRead(
            static_cast<std::uint32_t>(parsed)
        );
        applyResult(result);
    }

    void handleWrite()
    {
        std::uint64_t parsedAddress = 0;
        std::uint64_t parsedValue = 0;
        std::wstring error;
        if (!parseUnsigned(
                getText(IdAddress),
                std::numeric_limits<std::uint32_t>::max(),
                parsedAddress,
                error
            ))
        {
            showInputError(L"Register address: " + error);
            return;
        }
        if (!parseUnsigned(getText(IdValue), 255, parsedValue, error))
        {
            showInputError(L"Register value: " + error);
            return;
        }

        const RegisterOperationResult result = m_controller->registerWrite(
            static_cast<std::uint32_t>(parsedAddress),
            static_cast<std::uint8_t>(parsedValue)
        );
        applyResult(result);
    }

    void showInputError(const std::wstring& error)
    {
        m_operationVisual = -1;
        setText(IdLastOperationSuccess, L"false");
        setText(IdLastOperationStatus, L"LOCAL GUI input validation failed");
        setText(IdLastError, error);
        setText(IdEventLog, L"Input error: " + error);
        refreshConnectionState();
        refreshStatusColors();
    }

    void applyResult(const RegisterOperationResult& result)
    {
        m_operationVisual = result.success ? 1 : -1;
        std::wstring operation = widen(
            VtrxpCommunicationLib::registerOperationName(result.operation)
        );
        operation += L"  " + formatAddress(result.registerAddress);
        if (!result.valid && result.operation == RegisterOperation::Read)
            operation += L"  ->  N/A";
        else
            operation += L"  ->  " + formatByte(result.value);
        setText(IdLastOperation, operation);
        setText(IdLastOperationSuccess, result.success ? L"true" : L"false");
        setText(IdLastOperationStatus, widen(result.message));
        setText(IdLastError, result.success ? L"None" : widen(result.message));
        setText(IdGeneration, std::to_wstring(result.generation));

        const RegisterOperationResult lastRead = m_controller->lastValidRead();
        if (lastRead.success && lastRead.valid)
        {
            setText(IdLastReadAddress, formatAddress(lastRead.registerAddress));
            setText(IdLastReadValue, formatByte(lastRead.value));
        }

        ++m_monitoringStep;
        refreshConnectionState();
        updateMonitoring();

        std::wstring event = result.success
            ? L"SUCCESS \u2014 "
            : L"EXPLICIT FAILURE \u2014 ";
        event += widen(result.message);
        event += L"\r\nSIMULATED telemetry advanced deterministically; it is independent of the register address.";
        setText(IdEventLog, event);
        refreshStatusColors();
    }

    void clearDiagnostics()
    {
        m_operationVisual = 0;
        setText(IdLastReadAddress, L"\u2014");
        setText(IdLastReadValue, L"\u2014");
        setText(IdLastOperation, L"None");
        setText(IdLastOperationSuccess, L"\u2014");
        setText(IdLastOperationStatus, L"No operation has completed");
        setText(IdLastError, L"None");
        setText(IdGeneration, L"0");
        refreshStatusColors();
    }

    void refreshAll()
    {
        clearDiagnostics();
        refreshConnectionState();
        updateMonitoring();
        setText(
            IdEventLog,
            L"Ready. Click Connect / Start, then WRITE 0x2A = 0x5A and READ 0x2A. Use READ 0x100 for the explicit error demonstration."
        );
    }

    void refreshConnectionState()
    {
        setText(
            IdCommunicationState,
            widen(VtrxpCommunicationLib::communicationStateName(
                m_controller->communicationState()
            ))
        );
        const HWND stateControl = GetDlgItem(m_window, IdCommunicationState);
        if (stateControl)
            InvalidateRect(stateControl, nullptr, TRUE);
    }

    void refreshStatusColors()
    {
        for (const int id : {
                 IdLastOperationSuccess,
                 IdLastOperationStatus,
                 IdLastError
             })
        {
            const HWND control = GetDlgItem(m_window, id);
            if (control)
                InvalidateRect(control, nullptr, TRUE);
        }
    }

    void updateMonitoring()
    {
        static const std::array<double, 6> temperatures {
            22.5, 22.7, 22.9, 23.1, 22.8, 22.6
        };
        static const std::array<double, 6> supplies {
            2.500, 2.498, 2.502, 2.501, 2.499, 2.500
        };
        static const std::array<int, 6> rssiPercent {
            72, 74, 76, 75, 73, 72
        };
        static const std::array<std::array<bool, 4>, 6> txPatterns {{
            {{ true,  true,  false, true  }},
            {{ true,  true,  true,  true  }},
            {{ true,  false, true,  true  }},
            {{ true,  true,  true,  false }},
            {{ false, true,  true,  true  }},
            {{ true,  true,  false, true  }}
        }};

        const std::size_t index = m_monitoringStep % temperatures.size();
        const bool running =
            m_controller->communicationState() == CommunicationState::Running;
        const wchar_t* suffix = running
            ? L"  [SIM / LIVE]"
            : L"  [SIM / FROZEN]";

        std::wostringstream temperature;
        temperature << std::fixed << std::setprecision(1)
                    << temperatures[index] << L" \u00B0C" << suffix;
        setText(IdTemperature, temperature.str());

        std::wostringstream supply;
        supply << std::fixed << std::setprecision(3)
               << supplies[index] << L" V" << suffix;
        setText(IdSupply, supply.str());

        setText(
            IdRssi,
            L"level " + std::to_wstring(rssiPercent[index])
                + L" %" + suffix
        );
        const auto& channels = txPatterns[index];
        setText(
            IdTx1,
            channels[0] ? L"ENABLED  [SIMULATED]" : L"DISABLED  [SIMULATED]"
        );
        setText(
            IdTx2,
            channels[1] ? L"ENABLED  [SIMULATED]" : L"DISABLED  [SIMULATED]"
        );
        setText(
            IdTx3,
            channels[2] ? L"ENABLED  [SIMULATED]" : L"DISABLED  [SIMULATED]"
        );
        setText(
            IdTx4,
            channels[3] ? L"ENABLED  [SIMULATED]" : L"DISABLED  [SIMULATED]"
        );
    }

    void centerWindow()
    {
        RECT windowRectangle {};
        GetWindowRect(m_window, &windowRectangle);
        const int width = windowRectangle.right - windowRectangle.left;
        const int height = windowRectangle.bottom - windowRectangle.top;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        SetWindowPos(
            m_window, nullptr, x, y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }

    HINSTANCE m_instance { nullptr };
    HWND m_window { nullptr };
    HFONT m_normalFont { nullptr };
    HFONT m_boldFont { nullptr };
    HFONT m_titleFont { nullptr };
    HBRUSH m_headerBrush { nullptr };
    HBRUSH m_bannerBrush { nullptr };
    HBRUSH m_runningBrush { nullptr };
    HBRUSH m_errorBrush { nullptr };
    HBRUSH m_neutralBrush { nullptr };
    HBRUSH m_connectingBrush { nullptr };
    HBRUSH m_monitorBrush { nullptr };
    std::unique_ptr<VtrxpController> m_controller;
    std::uint64_t m_monitoringStep { 0 };
    int m_operationVisual { 0 };
};

}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR commandLine,
    int showCommand
)
{
    SetProcessDPIAware();
    LocalMockGui application(instance);
    if (!application.create(showCommand))
    {
        MessageBoxW(
            nullptr,
            L"Unable to create the LOCAL MOCK GUI.",
            L"HGTD VTRx+ LOCAL MOCK",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    const bool errorReady = commandLine
        && (std::wcsstr(commandLine, L"--error-ready")
            || std::wcsstr(commandLine, L"--capture-error-bmp"));
    const bool captureDemoReady = commandLine
        && std::wcsstr(commandLine, L"--capture-demo-ready-bmp");
    const bool capture = commandLine
        && (std::wcsstr(commandLine, L"--capture-bmp")
            || std::wcsstr(commandLine, L"--capture-error-bmp")
            || captureDemoReady);

    if (errorReady)
        application.loadErrorPresentationState();
    else if (captureDemoReady)
        application.prepareLiveDemo();
    else if (capture)
        application.loadSuccessfulPresentationState();
    else if (commandLine && std::wcsstr(commandLine, L"--demo-ready"))
        application.prepareLiveDemo();

    if (capture)
    {
        const std::wstring capturePath =
            L"vtrxp_mock_gui_capture_"
            + std::to_wstring(GetCurrentProcessId())
            + L".bmp";
        if (!application.captureSuccessfulPresentation(
                capturePath.c_str()
            ))
        {
            return 2;
        }
    }

    return application.run();
}
