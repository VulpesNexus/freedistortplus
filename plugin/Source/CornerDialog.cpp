// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vixen420
//
// FreeDistort+ is free software: you may redistribute it and/or
// modify it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. It comes with ABSOLUTELY NO WARRANTY. See the
// file LICENSE, or <https://www.gnu.org/licenses/>, for the full text.
//
// Additional permission under GPL-3.0 section 7: this file may be combined with
// the Adobe Illustrator SDK, whose sample framework sources are compiled into
// every plugin built from it. See LICENSE-EXCEPTION.

//  CornerDialog.cpp -- see CornerDialog.h.
//
//  A plain Win32 window built at run time, drawn in Illustrator's own dialog
//  colors, in the manner of LiveShear's dialog: owner-drawn buttons, check box
//  and radio buttons, because the stock ones paint system colors that no
//  message can change. Every text call uses the wide-character entry points.

#include "CornerDialog.h"
#include "DialogPlacement.h"

#ifdef WIN_ENV

#include <windows.h>
#include <commctrl.h>
#include <iterator>

#pragma comment(lib, "comctl32.lib")

namespace
{
    const int kIdEditBase = 2000;      // + corner * 2 + axis
    const int kIdAxisLabelBase = 2020; // + corner * 2 + axis
    const int kIdPosition = 2101;
    const int kIdOffset = 2102;
    const int kIdPreview = 2103;
    const int kIdUndistort = 2104;

    const wchar_t* const kClassName = L"VulpesNexusFreeDistortCorners";
    const wchar_t* const kTitle = L"Free Distort Corners";
    const wchar_t* const kCornerNames[4] = { L"Top Left", L"Top Right", L"Bottom Left", L"Bottom Right" };
    bool gClassRegistered = false;
    HINSTANCE gClassInstance = nullptr;

    // ---- layout, in pixels at 96 dots per inch ----------------------------

    const int kMargin = 12;
    const int kAxisLabelWidth = 22;
    const int kEditWidth = 88;
    const int kAxisGap = 18;
    const int kBlockWidth = (kAxisLabelWidth + kEditWidth) * 2 + kAxisGap;
    const int kBlockGap = 24;
    const int kEditHeight = 24;
    const int kRowHeight = 62;
    const int kButtonWidth = 80;
    const int kButtonHeight = 26;
    const int kClientWidth = kMargin * 2 + kBlockWidth * 2 + kBlockGap;
    const int kModeTop = kMargin + kRowHeight * 2 + 4;
    const int kButtonsTop = kModeTop + 32;
    const int kClientHeight = kButtonsTop + kButtonHeight + kMargin;

    struct DialogData
    {
        CornerDialogHost* host = nullptr;
        CornerDialogState* state = nullptr;
        fdmath::Quad current;
        /** The text this dialog last put in each field. A field whose text
            still matches has not been edited, and keeps the exact value. */
        std::wstring shown[8];
        HWND edits[8] = {};
        HWND axisLabels[8] = {};
        bool committed = false;
        bool finished = false;
        bool updating = false;
        bool previewShowsCurrent = false;
        /** Alt is held, so Cancel reads Reset. */
        bool altReset = false;
        fdmath::Quad previewed;

        int dpi = 96;
        HFONT font = nullptr;
        CornerDialogColors theme;
        HBRUSH backBrush = nullptr;
        HBRUSH editBrush = nullptr;
        int hot = 0;
    };

    HMENU ControlId(int id) { return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)); }

    CornerDialogColors SystemColorsUnlessSet(const CornerDialogColors& given)
    {
        if (given.set) return given;
        CornerDialogColors c;
        c.background = GetSysColor(COLOR_BTNFACE);
        c.text = GetSysColor(COLOR_BTNTEXT);
        c.editText = GetSysColor(COLOR_WINDOWTEXT);
        c.editBackground = GetSysColor(COLOR_WINDOW);
        c.border = GetSysColor(COLOR_3DSHADOW);
        c.focusRing = GetSysColor(COLOR_HOTLIGHT);
        c.control = GetSysColor(COLOR_BTNFACE);
        c.controlHot = GetSysColor(COLOR_3DLIGHT);
        c.controlPressed = GetSysColor(COLOR_3DSHADOW);
        return c;
    }

    /** Dark caption, on the Windows versions that can: attribute 20, and 19 for
        builds 17763 to 18985. */
    void ApplyDarkTitleBar(HWND hwnd)
    {
        typedef HRESULT (WINAPI *SetAttrProc)(HWND, DWORD, LPCVOID, DWORD);
        const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm == nullptr) return;
        const SetAttrProc setAttr = reinterpret_cast<SetAttrProc>(reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
        if (setAttr != nullptr)
        {
            const BOOL on = TRUE;
            setAttr(hwnd, 20, &on, sizeof(on));
            setAttr(hwnd, 19, &on, sizeof(on));
        }
        FreeLibrary(dwm);
    }
    int Px(int dip, int dpi) { return MulDiv(dip, dpi, 96); }

    fdmath::Pt BoundsCorner(const fdmath::Rect& b, int corner)
    {
        return fdmath::RectQuad(b).c[corner];
    }

    /** What a field holds, in points: a component of the display position,
        or of the display offset from the undistorted corner. */
    double FieldValue(DialogData* dd, int corner, int axis)
    {
        fdmath::Pt d = dd->host->ToDisplay(dd->current.c[corner]);
        if (dd->state->offsets) d = fdmath::Sub(d, dd->host->ToDisplay(BoundsCorner(dd->state->bounds, corner)));
        return axis == 0 ? d.h : d.v;
    }

    /** Sets one component of a corner from a field's value, leaving the other
        component exactly as it was. */
    void SetFieldValue(DialogData* dd, int corner, int axis, double value)
    {
        fdmath::Pt d = dd->host->ToDisplay(dd->current.c[corner]);
        double target = value;
        if (dd->state->offsets)
        {
            const fdmath::Pt base = dd->host->ToDisplay(BoundsCorner(dd->state->bounds, corner));
            target += axis == 0 ? base.h : base.v;
        }
        if (axis == 0) d.h = target; else d.v = target;
        const fdmath::Pt artwork = dd->host->FromDisplay(d);
        // The ruler maps horizontal to horizontal and vertical to vertical, so
        // only the component this field shows is taken from the conversion.
        if (axis == 0) dd->current.c[corner].h = artwork.h;
        else dd->current.c[corner].v = artwork.v;
    }

    void ShowField(DialogData* dd, int k)
    {
        dd->updating = true;
        dd->shown[k] = dd->host->FormatLength(FieldValue(dd, k / 2, k % 2));
        SetWindowTextW(dd->edits[k], dd->shown[k].c_str());
        dd->updating = false;
    }

    void ShowAll(DialogData* dd)
    {
        for (int k = 0; k < 8; ++k)
        {
            ShowField(dd, k);
            const wchar_t* label = (k % 2 == 0)
                ? (dd->state->offsets ? L"\x0394X" : L"X")
                : (dd->state->offsets ? L"\x0394Y" : L"Y");
            SetWindowTextW(dd->axisLabels[k], label);
        }
        const HWND position = GetDlgItem(GetParent(dd->edits[0]), kIdPosition);
        const HWND offset = GetDlgItem(GetParent(dd->edits[0]), kIdOffset);
        if (position) InvalidateRect(position, nullptr, TRUE);
        if (offset) InvalidateRect(offset, nullptr, TRUE);
    }

    void Republish(DialogData* dd)
    {
        if (dd->state->preview)
        {
            if (dd->previewShowsCurrent && fdmath::Near(dd->previewed, dd->current, 0.0)) return;
            dd->host->Preview(&dd->current);
            dd->previewed = dd->current;
            dd->previewShowsCurrent = true;
        }
        else if (dd->previewShowsCurrent)
        {
            dd->host->Preview(nullptr);
            dd->previewShowsCurrent = false;
        }
    }

    /** Takes a field's text into the quad, if it was edited. */
    void CommitField(DialogData* dd, int k)
    {
        if (dd->updating || k < 0 || k > 7) return;
        wchar_t buf[128] = { 0 };
        GetWindowTextW(dd->edits[k], buf, static_cast<int>(std::size(buf)));
        if (dd->shown[k] == buf) return;
        double points = 0.0;
        if (!dd->host->ParseLength(buf, &points))
        {
            // Unreadable text goes back to the value in effect.
            ShowField(dd, k);
            return;
        }
        SetFieldValue(dd, k / 2, k % 2, points);
        ShowField(dd, k);
        Republish(dd);
    }

    int FieldOf(HWND hwnd, DialogData* dd)
    {
        for (int k = 0; k < 8; ++k) if (dd->edits[k] == hwnd) return k;
        return -1;
    }

    /** Up and down arrows step a field by one unit of the document's ruler,
        by ten with Shift, the way Illustrator's own numeric fields do. */
    LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
    {
        DialogData* dd = reinterpret_cast<DialogData*>(ref);
        if (msg == WM_KEYDOWN && dd != nullptr && (wp == VK_UP || wp == VK_DOWN))
        {
            const int k = static_cast<int>(id) - kIdEditBase;
            CommitField(dd, k);
            double unit = 1.0;
            if (!dd->host->ParseLength(L"1", &unit) || !(unit > 0.0)) unit = 1.0;
            const double step = unit * ((GetKeyState(VK_SHIFT) < 0) ? 10.0 : 1.0);
            SetFieldValue(dd, k / 2, k % 2, FieldValue(dd, k / 2, k % 2) + (wp == VK_UP ? step : -step));
            ShowField(dd, k);
            Republish(dd);
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, EditSubclassProc, id);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK HoverSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
    {
        DialogData* dd = reinterpret_cast<DialogData*>(ref);
        if (msg == WM_MOUSEMOVE && dd != nullptr && dd->hot != static_cast<int>(id))
        {
            const HWND was = dd->hot != 0 ? GetDlgItem(GetParent(hwnd), dd->hot) : nullptr;
            dd->hot = static_cast<int>(id);
            if (was != nullptr) InvalidateRect(was, nullptr, TRUE);
            InvalidateRect(hwnd, nullptr, TRUE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        else if (msg == WM_MOUSELEAVE && dd != nullptr && dd->hot == static_cast<int>(id))
        {
            dd->hot = 0;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hwnd, HoverSubclassProc, id);
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    int DpiOf(HWND hwnd)
    {
        typedef UINT (WINAPI *GetDpiForWindowProc)(HWND);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != nullptr && hwnd != nullptr)
        {
            const GetDpiForWindowProc forWindow = reinterpret_cast<GetDpiForWindowProc>(
                reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
            if (forWindow != nullptr)
            {
                const UINT dpi = forWindow(hwnd);
                if (dpi >= 48) return static_cast<int>(dpi);
            }
        }
        const HDC screen = GetDC(nullptr);
        int dpi = 96;
        if (screen != nullptr)
        {
            dpi = GetDeviceCaps(screen, LOGPIXELSX);
            ReleaseDC(nullptr, screen);
        }
        return dpi >= 48 ? dpi : 96;
    }

    HFONT MakeUiFont(int dpi)
    {
        NONCLIENTMETRICSW metrics;
        ZeroMemory(&metrics, sizeof(metrics));
        metrics.cbSize = sizeof(metrics);
        LOGFONTW font;
        ZeroMemory(&font, sizeof(font));
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) font = metrics.lfMessageFont;
        else wcscpy_s(font.lfFaceName, L"Segoe UI");
        font.lfHeight = -MulDiv(9, dpi, 72);
        font.lfWidth = 0;
        return CreateFontIndirectW(&font);
    }

    void FillSolid(HDC dc, const RECT& rc, COLORREF color)
    {
        const HBRUSH brush = CreateSolidBrush(color);
        if (brush == nullptr) return;
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
    }

    void FrameThick(HDC dc, RECT rc, COLORREF color, int n)
    {
        const HBRUSH brush = CreateSolidBrush(color);
        if (brush == nullptr) return;
        for (int i = 0; i < n; ++i)
        {
            FrameRect(dc, &rc, brush);
            rc.left += 1; rc.top += 1; rc.right -= 1; rc.bottom -= 1;
        }
        DeleteObject(brush);
    }

    int Hairline(int dpi) { const int n = MulDiv(1, dpi, 96); return n < 1 ? 1 : n; }

    void DrawText(DialogData* dd, HDC dc, HWND control, RECT rc, COLORREF color, UINT format)
    {
        wchar_t text[64] = { 0 };
        GetWindowTextW(control, text, static_cast<int>(std::size(text)));
        const HGDIOBJ oldFont = dd->font != nullptr ? SelectObject(dc, dd->font) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, text, -1, &rc, format | DT_VCENTER | DT_SINGLELINE);
        if (oldFont != nullptr) SelectObject(dc, oldFont);
    }

    void DrawPushButton(DialogData* dd, const DRAWITEMSTRUCT* di)
    {
        const CornerDialogColors& t = dd->theme;
        const bool pressed = (di->itemState & ODS_SELECTED) != 0;
        const bool focused = (di->itemState & ODS_FOCUS) != 0;
        const bool hot = dd->hot == static_cast<int>(di->CtlID);
        COLORREF face = pressed ? t.controlPressed : (hot ? t.controlHot : t.control);
        RECT rc = di->rcItem;
        FillSolid(di->hDC, rc, face);
        const int line = Hairline(dd->dpi);
        FrameThick(di->hDC, rc, di->CtlID == IDOK ? t.focusRing : t.border, line);
        if (focused)
        {
            RECT ring = rc;
            InflateRect(&ring, -line * 2, -line * 2);
            FrameThick(di->hDC, ring, t.focusRing, line);
        }
        DrawText(dd, di->hDC, di->hwndItem, rc, t.text, DT_CENTER);
    }

    /** The Preview check box and the two mode buttons, drawn alike: a box or
        a ring, then the label. */
    void DrawToggle(DialogData* dd, const DRAWITEMSTRUCT* di, bool radio, bool on)
    {
        const CornerDialogColors& t = dd->theme;
        const bool focused = (di->itemState & ODS_FOCUS) != 0;
        const bool hot = dd->hot == static_cast<int>(di->CtlID);
        RECT rc = di->rcItem;
        FillSolid(di->hDC, rc, t.background);
        const int line = Hairline(dd->dpi);
        const int side = Px(13, dd->dpi);
        RECT box = { rc.left, rc.top + (rc.bottom - rc.top - side) / 2, 0, 0 };
        box.right = box.left + side;
        box.bottom = box.top + side;

        if (radio)
        {
            const HBRUSH fill = CreateSolidBrush(t.editBackground);
            const HPEN edge = CreatePen(PS_SOLID, line, hot ? t.focusRing : t.border);
            const HGDIOBJ oldBrush = SelectObject(di->hDC, fill);
            const HGDIOBJ oldPen = SelectObject(di->hDC, edge);
            Ellipse(di->hDC, box.left, box.top, box.right, box.bottom);
            if (on)
            {
                const HBRUSH dot = CreateSolidBrush(t.editText);
                SelectObject(di->hDC, dot);
                const int inset = side / 4 + 1;
                Ellipse(di->hDC, box.left + inset, box.top + inset, box.right - inset, box.bottom - inset);
                SelectObject(di->hDC, fill);
                DeleteObject(dot);
            }
            SelectObject(di->hDC, oldBrush);
            SelectObject(di->hDC, oldPen);
            DeleteObject(fill);
            DeleteObject(edge);
        }
        else
        {
            FillSolid(di->hDC, box, t.editBackground);
            FrameThick(di->hDC, box, hot ? t.focusRing : t.border, line);
            if (on)
            {
                const HPEN pen = CreatePen(PS_SOLID, line * 2, t.editText);
                const HGDIOBJ oldPen = SelectObject(di->hDC, pen);
                const int w = box.right - box.left;
                const POINT pts[3] = {
                    { box.left + w / 4, box.top + w / 2 },
                    { box.left + w / 2 - line, box.bottom - w / 3 },
                    { box.right - w / 5, box.top + w / 4 }
                };
                Polyline(di->hDC, pts, 3);
                SelectObject(di->hDC, oldPen);
                DeleteObject(pen);
            }
        }

        RECT label = rc;
        label.left = box.right + Px(6, dd->dpi);
        DrawText(dd, di->hDC, di->hwndItem, label, t.text, DT_LEFT);
        if (focused)
        {
            RECT ring = rc;
            InflateRect(&ring, -line, -line);
            FrameThick(di->hDC, ring, t.focusRing, line);
        }
    }

    HWND Make(HWND parent, HINSTANCE inst, const wchar_t* cls, const wchar_t* text, DWORD style,
              int x, int y, int w, int h, int id, int dpi)
    {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                               Px(x, dpi), Px(y, dpi), Px(w, dpi), Px(h, dpi), parent,
                               id ? ControlId(id) : nullptr, inst, nullptr);
    }

    LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        DialogData* dd = reinterpret_cast<DialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
            case WM_CREATE:
            {
                const CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                dd = static_cast<DialogData*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dd));
                const HINSTANCE inst = cs->hInstance;
                dd->dpi = DpiOf(hwnd);
                const int dpi = dd->dpi;
                dd->theme = SystemColorsUnlessSet(dd->state->colors);
                dd->backBrush = CreateSolidBrush(dd->theme.background);
                dd->editBrush = CreateSolidBrush(dd->theme.editBackground);
                if (dd->theme.dark) ApplyDarkTitleBar(hwnd);

                // The corners where they sit: top row above bottom row.
                for (int corner = 0; corner < 4; ++corner)
                {
                    const int x = kMargin + (corner % 2) * (kBlockWidth + kBlockGap);
                    const int y = kMargin + (corner / 2) * kRowHeight;
                    Make(hwnd, inst, L"STATIC", kCornerNames[corner], SS_LEFT, x, y, kBlockWidth, 18, 0, dpi);
                    for (int axis = 0; axis < 2; ++axis)
                    {
                        const int k = corner * 2 + axis;
                        const int ax = x + axis * (kAxisLabelWidth + kEditWidth + kAxisGap);
                        dd->axisLabels[k] = Make(hwnd, inst, L"STATIC", L"", SS_LEFT | SS_CENTERIMAGE,
                                                 ax, y + 22, kAxisLabelWidth, kEditHeight, kIdAxisLabelBase + k, dpi);
                        dd->edits[k] = Make(hwnd, inst, L"EDIT", L"", WS_TABSTOP | ES_RIGHT | ES_AUTOHSCROLL,
                                            ax + kAxisLabelWidth, y + 22, kEditWidth, kEditHeight, kIdEditBase + k, dpi);
                    }
                }
                Make(hwnd, inst, L"BUTTON", L"Position", WS_TABSTOP | BS_OWNERDRAW,
                     kMargin, kModeTop, 96, 22, kIdPosition, dpi);
                Make(hwnd, inst, L"BUTTON", L"Offset from the undistorted corner", WS_TABSTOP | BS_OWNERDRAW,
                     kMargin + 104, kModeTop, 260, 22, kIdOffset, dpi);
                Make(hwnd, inst, L"BUTTON", L"Preview", WS_TABSTOP | BS_OWNERDRAW,
                     kMargin, kButtonsTop, 96, kButtonHeight, kIdPreview, dpi);
                const int right = kClientWidth - kMargin;
                Make(hwnd, inst, L"BUTTON", L"Undistort", WS_TABSTOP | BS_OWNERDRAW,
                     right - kButtonWidth * 3 - 16, kButtonsTop, kButtonWidth, kButtonHeight, kIdUndistort, dpi);
                Make(hwnd, inst, L"BUTTON", L"Cancel", WS_TABSTOP | BS_OWNERDRAW,
                     right - kButtonWidth * 2 - 8, kButtonsTop, kButtonWidth, kButtonHeight, IDCANCEL, dpi);
                Make(hwnd, inst, L"BUTTON", L"OK", WS_TABSTOP | BS_OWNERDRAW,
                     right - kButtonWidth, kButtonsTop, kButtonWidth, kButtonHeight, IDOK, dpi);

                dd->font = MakeUiFont(dpi);
                if (dd->font != nullptr)
                    EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
                        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
                        return TRUE;
                    }, reinterpret_cast<LPARAM>(dd->font));

                for (int k = 0; k < 8; ++k)
                    SetWindowSubclass(dd->edits[k], EditSubclassProc, static_cast<UINT_PTR>(kIdEditBase + k), reinterpret_cast<DWORD_PTR>(dd));
                for (int id : { kIdPosition, kIdOffset, kIdPreview, kIdUndistort, static_cast<int>(IDCANCEL), static_cast<int>(IDOK) })
                    SetWindowSubclass(GetDlgItem(hwnd, id), HoverSubclassProc, static_cast<UINT_PTR>(id), reinterpret_cast<DWORD_PTR>(dd));

                ShowAll(dd);
                return 0;
            }

            case WM_ERASEBKGND:
                if (dd != nullptr && dd->backBrush != nullptr)
                {
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    FillRect(reinterpret_cast<HDC>(wp), &rc, dd->backBrush);
                    return 1;
                }
                break;

            case WM_PAINT:
                // The fields' borders, in the host's line color, drawn on the
                // dialog's face around each field.
                if (dd != nullptr)
                {
                    PAINTSTRUCT ps;
                    const HDC dc = BeginPaint(hwnd, &ps);
                    const int line = Hairline(dd->dpi);
                    for (HWND field : dd->edits)
                    {
                        if (field == nullptr) continue;
                        RECT rc;
                        GetWindowRect(field, &rc);
                        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rc), 2);
                        InflateRect(&rc, line, line);
                        FrameThick(dc, rc, GetFocus() == field ? dd->theme.focusRing : dd->theme.border, line);
                    }
                    EndPaint(hwnd, &ps);
                    return 0;
                }
                break;

            case WM_CTLCOLORSTATIC:
                if (dd != nullptr && dd->backBrush != nullptr)
                {
                    SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
                    SetTextColor(reinterpret_cast<HDC>(wp), dd->theme.text);
                    SetBkColor(reinterpret_cast<HDC>(wp), dd->theme.background);
                    return reinterpret_cast<LRESULT>(dd->backBrush);
                }
                break;

            case WM_CTLCOLOREDIT:
                if (dd != nullptr && dd->editBrush != nullptr)
                {
                    SetTextColor(reinterpret_cast<HDC>(wp), dd->theme.editText);
                    SetBkColor(reinterpret_cast<HDC>(wp), dd->theme.editBackground);
                    return reinterpret_cast<LRESULT>(dd->editBrush);
                }
                break;

            case WM_DRAWITEM:
                if (dd != nullptr)
                {
                    const DRAWITEMSTRUCT* di = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
                    if (di->CtlType == ODT_BUTTON)
                    {
                        if (di->CtlID == kIdPreview) DrawToggle(dd, di, false, dd->state->preview);
                        else if (di->CtlID == kIdPosition) DrawToggle(dd, di, true, !dd->state->offsets);
                        else if (di->CtlID == kIdOffset) DrawToggle(dd, di, true, dd->state->offsets);
                        else DrawPushButton(dd, di);
                        return TRUE;
                    }
                }
                break;

            case WM_COMMAND:
            {
                if (dd == nullptr) break;
                const int id = LOWORD(wp);
                if (id >= kIdEditBase && id < kIdEditBase + 8)
                {
                    if (HIWORD(wp) == EN_KILLFOCUS) { CommitField(dd, id - kIdEditBase); InvalidateRect(hwnd, nullptr, TRUE); }
                    else if (HIWORD(wp) == EN_SETFOCUS) InvalidateRect(hwnd, nullptr, TRUE);
                    return 0;
                }
                switch (id)
                {
                    case kIdPosition:
                    case kIdOffset:
                    {
                        const int focused = FieldOf(GetFocus(), dd);
                        if (focused >= 0) CommitField(dd, focused);
                        dd->state->offsets = (id == kIdOffset);
                        ShowAll(dd);
                        return 0;
                    }
                    case kIdPreview:
                        dd->state->preview = !dd->state->preview;
                        InvalidateRect(GetDlgItem(hwnd, kIdPreview), nullptr, TRUE);
                        Republish(dd);
                        return 0;
                    case kIdUndistort:
                        // Every corner back where it is with no distortion.
                        dd->current = fdmath::RectQuad(dd->state->bounds);
                        ShowAll(dd);
                        Republish(dd);
                        return 0;
                    case IDOK:
                    {
                        for (int k = 0; k < 8; ++k) CommitField(dd, k);
                        dd->committed = true;
                        DestroyWindow(hwnd);
                        return 0;
                    }
                    case IDCANCEL:
                        // With Alt held, Cancel reads Reset, as in Adobe's own
                        // dialogs: the fields go back to what they were when
                        // the dialog opened, and the dialog stays open.
                        if (dd->altReset && reinterpret_cast<HWND>(lp) == GetDlgItem(hwnd, IDCANCEL))
                        {
                            dd->current = dd->state->start;
                            ShowAll(dd);
                            Republish(dd);
                            return 0;
                        }
                        dd->committed = false;
                        DestroyWindow(hwnd);
                        return 0;
                    default:
                        break;
                }
                break;
            }

            case WM_CLOSE:
                if (dd) dd->committed = false;
                DestroyWindow(hwnd);
                return 0;

            case WM_DESTROY:
                if (dd) dd->finished = true;
                // Wake the modal loop without posting WM_QUIT, which belongs to
                // the application and not to one dialog.
                PostMessageW(nullptr, WM_NULL, 0, 0);
                return 0;

            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HINSTANCE OwnInstance()
    {
        HMODULE mod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&OwnInstance), &mod);
        return reinterpret_cast<HINSTANCE>(mod);
    }
}

bool RunCornerDialog(CornerDialogHost& host, CornerDialogState& state)
{
    const HINSTANCE inst = OwnInstance();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    if (!gClassRegistered)
    {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
        gClassRegistered = true;
        gClassInstance = inst;
    }

    const HWND parent = static_cast<HWND>(state.owner);

    DialogData dd;
    dd.host = &host;
    dd.state = &state;
    dd.current = state.start;
    // The document already shows the starting quad, so nothing is written
    // just because the dialog opened.
    dd.previewed = state.start;
    dd.previewShowsCurrent = true;

    const int dpi = DpiOf(parent);
    RECT rc = { 0, 0, Px(kClientWidth, dpi), Px(kClientHeight, dpi) };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    const POINT origin = DialogPlacement::Origin(parent, rc.right - rc.left, rc.bottom - rc.top);

    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT | (state.activate ? 0 : WS_EX_NOACTIVATE);
    const HWND hwnd = CreateWindowExW(exStyle, kClassName, kTitle, WS_POPUPWINDOW | WS_CAPTION,
                                      origin.x, origin.y, rc.right - rc.left, rc.bottom - rc.top,
                                      parent, nullptr, inst, &dd);
    if (hwnd == nullptr) return false;
    ShowWindow(hwnd, state.activate ? SW_SHOW : SW_SHOWNOACTIVATE);

    if (parent) EnableWindow(parent, FALSE);
    const int focus = (state.focusCorner >= 0 && state.focusCorner < 4 ? state.focusCorner : 0) * 2;
    if (state.activate)
    {
        SetFocus(dd.edits[focus]);
        SendMessageW(dd.edits[focus], EM_SETSEL, 0, -1);
    }

    MSG msg;
    while (!dd.finished)
    {
        if (!GetMessageW(&msg, nullptr, 0, 0))
        {
            // The application is quitting: put the message back for its own
            // loop and close as a cancel.
            PostQuitMessage(static_cast<int>(msg.wParam));
            dd.committed = false;
            if (IsWindow(hwnd)) DestroyWindow(hwnd);
            break;
        }
        if (dd.finished) break;

        // Alt turns Cancel into Reset while it is held. Its key messages are
        // not passed on: on their own they would put the window's system menu
        // into keyboard mode.
        if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) && msg.wParam == VK_MENU)
        {
            const bool held = msg.message == WM_SYSKEYDOWN;
            if (held != dd.altReset)
            {
                dd.altReset = held;
                const HWND cancel = GetDlgItem(hwnd, IDCANCEL);
                SetWindowTextW(cancel, held ? L"Reset" : L"Cancel");
                InvalidateRect(cancel, nullptr, TRUE);
            }
            continue;
        }

        const int field = FieldOf(msg.hwnd, &dd);
        // IsDialogMessage would take the arrows for moving between controls.
        const bool nudge = msg.message == WM_KEYDOWN && (msg.wParam == VK_UP || msg.wParam == VK_DOWN) && field >= 0;

        // Enter: a focused push button presses itself; anywhere else it is OK,
        // after the field being typed in has been taken.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN)
        {
            const HWND focused = GetFocus();
            const int id = focused != nullptr ? GetDlgCtrlID(focused) : 0;
            const bool onButton = id == kIdUndistort || id == IDCANCEL || id == IDOK || id == kIdPreview ||
                                  id == kIdPosition || id == kIdOffset;
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(onButton ? id : IDOK, BN_CLICKED),
                         reinterpret_cast<LPARAM>(onButton ? focused : nullptr));
            continue;
        }

        if (nudge || !IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (dd.font != nullptr) DeleteObject(dd.font);
    if (dd.backBrush != nullptr) DeleteObject(dd.backBrush);
    if (dd.editBrush != nullptr) DeleteObject(dd.editBrush);
    if (parent)
    {
        EnableWindow(parent, TRUE);
        if (state.activate) SetForegroundWindow(parent);
    }

    state.result = dd.current;
    return dd.committed;
}

void ShutdownCornerDialog()
{
    if (!gClassRegistered) return;
    UnregisterClassW(kClassName, gClassInstance);
    gClassRegistered = false;
    gClassInstance = nullptr;
}

#else // !WIN_ENV

bool RunCornerDialog(CornerDialogHost&, CornerDialogState&) { return false; }
void ShutdownCornerDialog() {}

#endif
