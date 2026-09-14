#pragma once

#include <Windows.h>
#include <memory>
#include "CpuMonitor.h"

// Top-level window that renders one scrolling usage graph per CPU core
// using GDI, double-buffered via a memory DC to avoid flicker.
class GraphWindow
{
public:
    GraphWindow();
    ~GraphWindow();

    bool Create(HINSTANCE hInstance, int nCmdShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnCreate(HWND hwnd);
    void OnTimer();
    void OnPaint(HWND hwnd);
    void OnSize(int width, int height);
    void OnDestroy();

    void Render(HDC hdc, RECT clientRect);
    void DrawCorePanel(HDC hdc, const RECT& panel, size_t coreIndex);
    void ResizeBackBuffer(HDC referenceDc, int width, int height);
    void CreateSegmentBrush();
    static HFONT CreateMonospaceFont(int pointHeight, int weight);

    static constexpr UINT_PTR kTimerId = 1;
    static constexpr UINT kUpdateIntervalMs = 1000;

    HWND m_hwnd = nullptr;
    CpuMonitor m_monitor;

    // Double buffer
    HBITMAP m_backBuffer = nullptr;
    HDC m_backBufferDc = nullptr;
    int m_backBufferWidth = 0;
    int m_backBufferHeight = 0;

    HFONT m_labelFont = nullptr;
    HFONT m_titleFont = nullptr;

    // Pattern brush that tiles alternating "lit segment" / "gap" rows,
    // giving bars a discrete, LED-matrix-like look instead of a solid fill.
    HBRUSH m_segmentBrush = nullptr;

    bool m_hasData = false;
};
