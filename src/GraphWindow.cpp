#include "GraphWindow.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <vector>

namespace
{
    const wchar_t kWindowClassName[] = L"ActivityMonitorMainWindow";

    // Classic "terminal" theme: light green bars on a dark green background.
    constexpr COLORREF kBarColor = RGB(0x39, 0xDF, 0x14);
    constexpr COLORREF kBackgroundColor = RGB(0x03, 0x22, 0x03);
    constexpr COLORREF kPanelBgColor = RGB(0x06, 0x1F, 0x06);
    constexpr COLORREF kGridColor = RGB(0x14, 0x3D, 0x14);
    constexpr COLORREF kTextColor = RGB(0x8F, 0xE6, 0x7D);
    constexpr COLORREF kMutedTextColor = RGB(0x3E, 0x7A, 0x3E);

    constexpr int kPanelMargin = 6;
    constexpr int kPanelPaddingTop = 22; // space reserved for the title/label line
    constexpr int kPanelPaddingBottom = 4;
    constexpr int kPanelPaddingSides = 4;

    // Discrete "LED segment" look for the bars: each segment is a lit block
    // followed by a thin gap, repeating vertically.
    constexpr int kSegmentLitHeight = 3;
    constexpr int kSegmentGapHeight = 2;
    constexpr int kSegmentPitch = kSegmentLitHeight + kSegmentGapHeight;

    // Bars themselves have a fixed pixel width and gap, independent of the
    // panel's size - resizing the window changes how many bars fit, not how
    // wide each one is.
    constexpr int kBarWidth = 3;
    constexpr int kBarGap = 2;
    constexpr int kBarPitch = kBarWidth + kBarGap;

    // Tray icon plumbing.
    constexpr UINT WM_TRAYICON = WM_APP + 1;
    constexpr UINT_PTR kTrayIconId = 1;
    constexpr UINT kMenuIdShow = 1001;
    constexpr UINT kMenuIdExit = 1002;
}

GraphWindow::GraphWindow() = default;

GraphWindow::~GraphWindow()
{
    if (m_backBufferDc)
        DeleteDC(m_backBufferDc);
    if (m_backBuffer)
        DeleteObject(m_backBuffer);
    if (m_labelFont)
        DeleteObject(m_labelFont);
    if (m_titleFont)
        DeleteObject(m_titleFont);
    if (m_segmentBrush)
        DeleteObject(m_segmentBrush);
}

LRESULT CALLBACK GraphWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    GraphWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<GraphWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<GraphWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT GraphWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;
    case WM_TIMER:
        if (wParam == kTimerId)
            OnTimer();
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_ERASEBKGND:
        return 1; // avoid flicker; we paint the whole client area ourselves
    case WM_CLOSE:
        // Hide to the tray instead of closing; the app keeps running in the
        // background until "Exit" is chosen from the tray context menu.
        HideToTray();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE)
        {
            HideToTray();
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    case WM_TRAYICON:
        OnTrayIconMessage(lParam);
        return 0;
    case WM_COMMAND:
        if (HandleTrayMenuCommand(LOWORD(wParam)))
            return 0;
        return DefWindowProc(hwnd, msg, wParam, lParam);
    case WM_DESTROY:
        OnDestroy();
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

bool GraphWindow::Create(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &GraphWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    if (!wc.hIcon)
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassEx(&wc))
        return false;

    if (!m_monitor.Initialize())
    {
        MessageBox(nullptr, L"Failed to initialize CPU performance counters (PDH).",
                   L"Activity Monitor", MB_ICONERROR | MB_OK);
        return false;
    }

    m_hwnd = CreateWindowEx(
        0, kWindowClassName, L"CPU Activity Monitor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 650,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd)
        return false;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

int GraphWindow::RunMessageLoop()
{
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// Tries a list of monospaced, "technical"-looking fonts in order of
// preference and returns the first one actually installed on the system.
// GDI silently substitutes a fallback font for an unknown face name instead
// of failing, so we have to check the face that was actually selected
// (GetTextFace) rather than trust CreateFont's return value.
HFONT GraphWindow::CreateMonospaceFont(int pointHeight, int weight)
{
    static const wchar_t* const kCandidates[] = {
        L"IBM Plex Mono", // preferred - install it for the intended look
        L"Cascadia Mono",
        L"Consolas",
        L"Courier New", // always present on Windows; final fallback
    };

    HDC screenDc = GetDC(nullptr);

    HFONT chosen = nullptr;
    for (const wchar_t* candidate : kCandidates)
    {
        HFONT font = CreateFont(
            -pointHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, candidate);

        HGDIOBJ oldFont = SelectObject(screenDc, font);
        wchar_t actualFace[LF_FACESIZE] = {};
        GetTextFace(screenDc, LF_FACESIZE, actualFace);
        SelectObject(screenDc, oldFont);

        bool matches = _wcsicmp(actualFace, candidate) == 0;
        if (matches || candidate == kCandidates[std::size(kCandidates) - 1])
        {
            chosen = font;
            break;
        }
        DeleteObject(font);
    }

    ReleaseDC(nullptr, screenDc);
    return chosen;
}

void GraphWindow::OnCreate(HWND hwnd)
{
    // WM_CREATE fires synchronously inside CreateWindowEx, before it has
    // returned and assigned m_hwnd in Create() - anything here that needs
    // the window handle (SetupTrayIcon, in particular) must use this local
    // parameter rather than the not-yet-set m_hwnd member.
    m_hwnd = hwnd;

    m_labelFont = CreateMonospaceFont(14, FW_NORMAL);
    m_titleFont = CreateMonospaceFont(14, FW_SEMIBOLD);

    CreateSegmentBrush();
    SetupTrayIcon();

    SetTimer(hwnd, kTimerId, kUpdateIntervalMs, nullptr);

    // Take one immediate sample so the window isn't empty for a full second.
    m_monitor.Update();
    m_hasData = true;
    UpdateTrayIcon();
}

void GraphWindow::CreateSegmentBrush()
{
    // Build a 1 x kSegmentPitch tile: the bottom kSegmentLitHeight rows are
    // the bar color, the remaining top rows are the panel background. A
    // pattern brush made from this tiles vertically forever, so filling a
    // bar's rect with it (with the brush origin anchored to the plot's
    // bottom edge) produces evenly spaced lit segments automatically -
    // no per-segment drawing needed.
    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP tile = CreateCompatibleBitmap(screenDc, 1, kSegmentPitch);
    HGDIOBJ oldBmp = SelectObject(memDc, tile);

    RECT full{0, 0, 1, kSegmentPitch};
    HBRUSH gapBrush = CreateSolidBrush(kPanelBgColor);
    FillRect(memDc, &full, gapBrush);
    DeleteObject(gapBrush);

    RECT lit{0, kSegmentGapHeight, 1, kSegmentPitch};
    HBRUSH litBrush = CreateSolidBrush(kBarColor);
    FillRect(memDc, &lit, litBrush);
    DeleteObject(litBrush);

    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    m_segmentBrush = CreatePatternBrush(tile);
    DeleteObject(tile); // CreatePatternBrush keeps its own copy of the bitmap
}

void GraphWindow::OnTimer()
{
    if (m_monitor.Update())
        m_hasData = true;

    wchar_t title[128];
    swprintf_s(title, L"CPU Activity Monitor - Total: %.0f%%", m_monitor.GetTotalUsage());
    SetWindowText(m_hwnd, title);

    UpdateTrayIcon();

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void GraphWindow::ResizeBackBuffer(HDC referenceDc, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    if (m_backBufferDc && width == m_backBufferWidth && height == m_backBufferHeight)
        return;

    if (m_backBufferDc)
    {
        DeleteDC(m_backBufferDc);
        m_backBufferDc = nullptr;
    }
    if (m_backBuffer)
    {
        DeleteObject(m_backBuffer);
        m_backBuffer = nullptr;
    }

    m_backBufferDc = CreateCompatibleDC(referenceDc);
    m_backBuffer = CreateCompatibleBitmap(referenceDc, width, height);
    SelectObject(m_backBufferDc, m_backBuffer);

    m_backBufferWidth = width;
    m_backBufferHeight = height;
}

void GraphWindow::OnSize(int /*width*/, int /*height*/)
{
    if (m_hwnd)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void GraphWindow::OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    ResizeBackBuffer(hdc, width, height);

    if (m_backBufferDc)
    {
        Render(m_backBufferDc, clientRect);
        BitBlt(hdc, 0, 0, width, height, m_backBufferDc, 0, 0, SRCCOPY);
    }

    EndPaint(hwnd, &ps);
}

void GraphWindow::OnDestroy()
{
    RemoveTrayIcon();
    KillTimer(m_hwnd, kTimerId);
    PostQuitMessage(0);
}

// --- System tray -----------------------------------------------------------

void GraphWindow::SetupTrayIcon()
{
    ZeroMemory(&m_trayIconData, sizeof(m_trayIconData));
    m_trayIconData.cbSize = sizeof(m_trayIconData);
    m_trayIconData.hWnd = m_hwnd;
    m_trayIconData.uID = kTrayIconId;
    m_trayIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_trayIconData.uCallbackMessage = WM_TRAYICON;
    m_trayIconData.hIcon = CreateTrayIconForUsage(0.0);
    wcscpy_s(m_trayIconData.szTip, L"CPU Activity Monitor");

    m_trayIconVisible = Shell_NotifyIcon(NIM_ADD, &m_trayIconData) != FALSE;

    if (m_trayIconData.hIcon)
    {
        DestroyIcon(m_trayIconData.hIcon);
        m_trayIconData.hIcon = nullptr;
    }
}

void GraphWindow::UpdateTrayIcon()
{
    if (!m_trayIconVisible)
        return;

    double avg = m_monitor.GetTotalUsage();

    HICON icon = CreateTrayIconForUsage(avg);
    m_trayIconData.uFlags = NIF_ICON | NIF_TIP;
    m_trayIconData.hIcon = icon;
    swprintf_s(m_trayIconData.szTip, L"CPU Activity Monitor - %.0f%%", avg);

    Shell_NotifyIcon(NIM_MODIFY, &m_trayIconData);

    // Shell_NotifyIcon copies what it needs internally, so the icon handle
    // can (and should) be freed right away rather than accumulating GDI
    // handles - a fresh one is created on every tick anyway.
    if (icon)
        DestroyIcon(icon);
    m_trayIconData.hIcon = nullptr;
}

void GraphWindow::RemoveTrayIcon()
{
    if (!m_trayIconVisible)
        return;

    Shell_NotifyIcon(NIM_DELETE, &m_trayIconData);
    m_trayIconVisible = false;
}

void GraphWindow::ShowMainWindow()
{
    ShowWindow(m_hwnd, SW_RESTORE);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void GraphWindow::HideToTray()
{
    ShowWindow(m_hwnd, SW_HIDE);
}

void GraphWindow::ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenu(menu, MF_STRING, kMenuIdShow, L"Show");
    AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenu(menu, MF_STRING, kMenuIdExit, L"Exit");

    // Required so the popup menu closes properly if the user clicks away
    // from it instead of choosing an item (standard tray-icon menu dance).
    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessage(m_hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

void GraphWindow::OnTrayIconMessage(LPARAM lParam)
{
    switch (lParam)
    {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        ShowMainWindow();
        break;
    case WM_RBUTTONUP:
        ShowTrayMenu();
        break;
    default:
        break;
    }
}

bool GraphWindow::HandleTrayMenuCommand(UINT commandId)
{
    switch (commandId)
    {
    case kMenuIdShow:
        ShowMainWindow();
        return true;
    case kMenuIdExit:
        DestroyWindow(m_hwnd);
        return true;
    default:
        return false;
    }
}

// Draws a small live gauge into a tray-icon-sized bitmap: a bar filling from
// the bottom in proportion to the overall (all-core average) CPU usage, in
// the app's own light-green-on-dark-green theme. Called every timer tick, so
// the tray icon animates in step with the main window's bar charts.
HICON GraphWindow::CreateTrayIconForUsage(double averagePercent)
{
    HDC screenDc = GetDC(nullptr);

    int w = GetSystemMetrics(SM_CXSMICON);
    int h = GetSystemMetrics(SM_CYSMICON);
    if (w <= 0) w = 16;
    if (h <= 0) h = 16;

    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP colorBmp = CreateCompatibleBitmap(screenDc, w, h);
    HGDIOBJ oldBmp = SelectObject(memDc, colorBmp);

    RECT full{0, 0, w, h};
    HBRUSH bgBrush = CreateSolidBrush(kPanelBgColor);
    FillRect(memDc, &full, bgBrush);
    DeleteObject(bgBrush);

    double v = std::clamp(averagePercent, 0.0, 100.0);
    int margin = std::max(1, w / 8);
    int barAreaHeight = h - 2 * margin;
    int barHeight = static_cast<int>(std::lround(v / 100.0 * barAreaHeight));
    if (v > 0.0 && barHeight < 1)
        barHeight = 1; // always show at least a sliver for any non-zero load

    if (barHeight > 0)
    {
        RECT bar{margin, h - margin - barHeight, w - margin, h - margin};
        HBRUSH barBrush = CreateSolidBrush(kBarColor);
        FillRect(memDc, &bar, barBrush);
        DeleteObject(barBrush);
    }

    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    // A fully black AND-mask makes the color bitmap opaque everywhere -
    // there's no need for real transparency since the background fill above
    // already covers the whole icon square.
    HBITMAP maskBmp = CreateBitmap(w, h, 1, 1, nullptr);
    HDC maskDc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldMaskBmp = SelectObject(maskDc, maskBmp);
    RECT maskFull{0, 0, w, h};
    FillRect(maskDc, &maskFull, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SelectObject(maskDc, oldMaskBmp);
    DeleteDC(maskDc);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = maskBmp;
    ii.hbmColor = colorBmp;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(colorBmp);
    DeleteObject(maskBmp);

    return icon;
}

void GraphWindow::Render(HDC hdc, RECT clientRect)
{
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    HBRUSH bgBrush = CreateSolidBrush(kBackgroundColor);
    FillRect(hdc, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    size_t coreCount = m_monitor.GetCoreCount();
    if (coreCount == 0)
        return;

    // Determine a roughly-square grid of panels.
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(coreCount))));
    cols = std::max(cols, 1);
    int rows = static_cast<int>(std::ceil(static_cast<double>(coreCount) / cols));

    int panelWidth = width / cols;
    int panelHeight = height / rows;

    SetBkMode(hdc, TRANSPARENT);

    for (size_t i = 0; i < coreCount; ++i)
    {
        int col = static_cast<int>(i) % cols;
        int row = static_cast<int>(i) / cols;

        RECT panel;
        panel.left = col * panelWidth;
        panel.top = row * panelHeight;
        panel.right = (col == cols - 1) ? width : (panel.left + panelWidth);
        panel.bottom = (row == rows - 1) ? height : (panel.top + panelHeight);

        DrawCorePanel(hdc, panel, i);
    }
}

void GraphWindow::DrawCorePanel(HDC hdc, const RECT& panelOuter, size_t coreIndex)
{
    RECT panel = panelOuter;
    InflateRect(&panel, -kPanelMargin, -kPanelMargin);
    if (panel.right <= panel.left || panel.bottom <= panel.top)
        return;

    // Panel background
    HBRUSH panelBrush = CreateSolidBrush(kPanelBgColor);
    FillRect(hdc, &panel, panelBrush);
    DeleteObject(panelBrush);

    HPEN borderPen = CreatePen(PS_SOLID, 1, kGridColor);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    double usage = m_monitor.GetCoreUsage(coreIndex);

    // Title / current value label
    RECT labelRect = panel;
    labelRect.left += kPanelPaddingSides;
    labelRect.right -= kPanelPaddingSides;
    labelRect.top += 2;
    labelRect.bottom = labelRect.top + (kPanelPaddingTop - 4);

    wchar_t label[64];
    swprintf_s(label, L"Core %zu", coreIndex);

    HGDIOBJ oldFont = SelectObject(hdc, m_titleFont);
    SetTextColor(hdc, kTextColor);
    RECT nameRect = labelRect;
    DrawText(hdc, label, -1, &nameRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    wchar_t pct[32];
    swprintf_s(pct, L"%.0f%%", usage);
    SetTextColor(hdc, kBarColor);
    RECT pctRect = labelRect;
    DrawText(hdc, pct, -1, &pctRect, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldFont);

    // Graph plot area
    RECT plot;
    plot.left = panel.left + kPanelPaddingSides;
    plot.right = panel.right - kPanelPaddingSides;
    plot.top = panel.top + kPanelPaddingTop;
    plot.bottom = panel.bottom - kPanelPaddingBottom;

    if (plot.right <= plot.left || plot.bottom <= plot.top)
        return;

    int plotWidth = plot.right - plot.left;
    int plotHeight = plot.bottom - plot.top;

    // Horizontal gridlines at 0/25/50/75/100%
    HPEN gridPen = CreatePen(PS_SOLID, 1, kGridColor);
    HGDIOBJ oldGridPen = SelectObject(hdc, gridPen);
    HGDIOBJ oldGridFont = SelectObject(hdc, m_labelFont);
    SetTextColor(hdc, kMutedTextColor);

    for (int p = 0; p <= 100; p += 25)
    {
        int y = plot.bottom - static_cast<int>(std::lround(p / 100.0 * plotHeight));
        MoveToEx(hdc, plot.left, y, nullptr);
        LineTo(hdc, plot.right, y);
    }
    SelectObject(hdc, oldGridPen);
    DeleteObject(gridPen);
    SelectObject(hdc, oldGridFont);

    // Usage history as a scrolling bar chart. Bars have a fixed pixel width
    // and gap (kBarWidth / kBarGap) regardless of panel size, so resizing
    // the window changes how many bars fit, never how wide they are. Bars
    // are right-anchored (newest at the plot's right edge) and filled with
    // a segmented pattern brush so each one renders as a stack of discrete
    // lit blocks (LED-matrix style) rather than a solid area. The brush
    // tiles vertically from y=0, so anchor its origin to this panel's
    // baseline (plot.bottom) before filling.
    const std::deque<double>& history = m_monitor.GetCoreHistory(coreIndex);
    if (!history.empty() && m_segmentBrush)
    {
        size_t n = history.size();
        int maxVisibleBars = std::max(0, plotWidth / kBarPitch);
        size_t shown = std::min(static_cast<size_t>(maxVisibleBars), n);

        POINT prevOrg;
        SetBrushOrgEx(hdc, 0, plot.bottom, &prevOrg);

        for (size_t k = 0; k < shown; ++k)
        {
            // k = 0 is the newest sample, anchored at the right edge;
            // increasing k steps left through older samples.
            double v = std::clamp(history[n - 1 - k], 0.0, 100.0);

            int xRight = plot.right - static_cast<int>(k) * kBarPitch;
            int xLeft = xRight - kBarWidth;

            int barHeight = static_cast<int>(std::lround(v / 100.0 * plotHeight));
            // Round up to a whole number of segments so even a tiny
            // non-zero reading shows at least one lit block.
            if (barHeight > 0)
                barHeight = ((barHeight + kSegmentPitch - 1) / kSegmentPitch) * kSegmentPitch;
            int yTop = plot.bottom - barHeight;

            RECT barRect{xLeft, yTop, xRight, plot.bottom};
            if (barRect.top < barRect.bottom)
                FillRect(hdc, &barRect, m_segmentBrush);
        }

        SetBrushOrgEx(hdc, prevOrg.x, prevOrg.y, nullptr);
    }
}
