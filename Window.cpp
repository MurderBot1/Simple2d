#ifndef WINDOW_CPP
#define WINDOW_CPP

#include "Window.h"

Window::Window(HINSTANCE hInst, int width, int height, bool fullscreen)
    : hInstance(hInst), hwnd(nullptr), running(true), fullscreen(false)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = Window::windowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"MyWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    hwnd = CreateWindowW(
        wc.lpszClassName,
        L"",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create window!", L"Error", MB_ICONERROR);
        running = false;
        return;
    }

    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
    
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    createBackBuffer(width, height);

    if(fullscreen) setFullscreen(true);

    lastFrame = std::chrono::high_resolution_clock::now();
}

Window::~Window() {
    if (imageDC) { DeleteDC(imageDC); imageDC = nullptr; }
    if (backDC && backOldBitmap) {
        SelectObject(backDC, backOldBitmap);
    }
    if (backBitmap) DeleteObject(backBitmap);
    if (backDC) DeleteDC(backDC);

}

void Window::writeBackground(color c) {
    uint32_t packed = (c.r) | (c.g << 8) | (c.b << 16);
    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);
    size_t count = bufferWidth * bufferHeight;

    __m128i fill = _mm_set1_epi32(packed);
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        _mm_storeu_si128((__m128i*)&pixels[i], fill);
    }
    for (; i < count; ++i) {
        pixels[i] = packed;
    }
    markDirty(0, 0, bufferWidth, bufferHeight);
    isAllDirty = true;
}

void Window::writePoint(int x, int y, color c) {
    if ((unsigned)x >= (unsigned)bufferWidth || (unsigned)y >= (unsigned)bufferHeight) return;
    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);
    pixels[y * bufferWidth + x] = (c.r) | (c.g << 8) | (c.b << 16); // 0x00BBGGR
    markDirty(x, y, 1, 1);
}

void Window::writeLine(int x1, int y1, int x2, int y2, color c) {
    uint32_t packed = (c.r) | (c.g << 8) | (c.b << 16);
    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);

    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        if ((unsigned)x1 < (unsigned)bufferWidth && (unsigned)y1 < (unsigned)bufferHeight) {
            pixels[y1 * bufferWidth + x1] = packed;
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }

    if (useMarkDirty) {
        int left   = fastMin(x1, x2);
        int top    = fastMin(y1, y2);
        int right  = fastMax(x1, x2);
        int bottom = fastMax(y1, y2);
        markDirty(left, top, right - left + 1, bottom - top + 1);
    }
}

void Window::writeSquare(int x, int y, int scale, color c) {
    writeRect(x, y, scale, scale, c);
}

void Window::writeRect(int x, int y, int w, int h, color c) {
    int startX = fastMax(0, x);
    int startY = fastMax(0, y);
    int endX   = fastMin(bufferWidth,  x + w);
    int endY   = fastMin(bufferHeight, y + h);
    if (startX >= endX || startY >= endY) return;

    uint32_t packed = (c.r) | (c.g << 8) | (c.b << 16);
    __m128i fill = _mm_set1_epi32(packed);

    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);
    for (int row = startY; row < endY; ++row) {
        uint32_t* dst = pixels + row * bufferWidth + startX;
        int len = endX - startX;
        int i = 0;
        for (; i + 4 <= len; i += 4) {
            _mm_storeu_si128((__m128i*)&dst[i], fill);
        }
        for (; i < len; ++i) {
            dst[i] = packed;
        }
    }
    markDirty(x, y, w, h);
}

struct Edge { int yMin, yMax, x; float invSlope; };

void Window::writePolygon(const std::vector<POINT>& pts, color c) {
    if (pts.size() < 3) return;
    uint32_t packed = (c.r) | (c.g << 8) | (c.b << 16);
    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);

    // Build edge table
    std::vector<Edge> edges;
    for (size_t i = 0; i < pts.size(); ++i) {
        POINT p1 = pts[i];
        POINT p2 = pts[(i+1)%pts.size()];
        if (p1.y == p2.y) continue; // skip horizontals
        if (p1.y > p2.y) std::swap(p1, p2);
        Edge e;
        e.yMin = p1.y;
        e.yMax = p2.y;
        e.x = p1.x;
        e.invSlope = float(p2.x - p1.x) / float(p2.y - p1.y);
        edges.push_back(e);
    }

    int yMin = INT_MAX, yMax = INT_MIN;
    for (auto& e : edges) {
        yMin = fastMin(yMin, e.yMin);
        yMax = fastMax(yMax, e.yMax);
    }

    // Scanline fill
    for (int y = yMin; y < yMax; ++y) {
        std::vector<int> xInts;
        for (auto& e : edges) {
            if (y >= e.yMin && y < e.yMax) {
                xInts.push_back(int(e.x + (y - e.yMin) * e.invSlope));
            }
        }
        std::sort(xInts.begin(), xInts.end());
        for (size_t i = 0; i+1 < xInts.size(); i += 2) {
            int xL = fastMax(0, xInts[i]);
            int xR = fastMin(bufferWidth-1, xInts[i+1]);
            if (y >= 0 && y < bufferHeight) {
                for (int x = xL; x <= xR; ++x) {
                    pixels[y * bufferWidth + x] = packed;
                }
            }
        }
    }

    int minX = pts[0].x, maxX = pts[0].x;
    int minY = pts[0].y, maxY = pts[0].y;
    for (int i = 1; i < pts.size(); ++i) {
        minX = fastMin((long)minX, pts[i].x);
        maxX = fastMax((long)maxX, pts[i].x);
        minY = fastMin((long)minY, pts[i].y);
        maxY = fastMax((long)maxY, pts[i].y);
    }
    markDirty(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

inline void Window::plotAA(int x, int y, float c, uint32_t packed) {
    if ((unsigned)x >= (unsigned)bufferWidth || (unsigned)y >= (unsigned)bufferHeight) return;

    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);

    // Extract RGB
    uint8_t sr = packed & 0xFF;
    uint8_t sg = (packed >> 8) & 0xFF;
    uint8_t sb = (packed >> 16) & 0xFF;

    uint32_t dst = pixels[y * bufferWidth + x];
    uint8_t dr = dst & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = (dst >> 16) & 0xFF;

    uint8_t nr = uint8_t(sr * c + dr * (1 - c));
    uint8_t ng = uint8_t(sg * c + dg * (1 - c));
    uint8_t nb = uint8_t(sb * c + db * (1 - c));

    pixels[y * bufferWidth + x] = nr | (ng << 8) | (nb << 16);
}

void Window::writeCircle(int cx, int cy, int radius, color col) {
    uint32_t packed = (col.r) | (col.g << 8) | (col.b << 16);
    auto* pixels = static_cast<uint32_t*>(pixelBuffer);

    // --- Step 1: fill interior with solid spans ---
    for (int yy = -radius; yy <= radius; ++yy) {
        int yAbs = cy + yy;
        if (yAbs < 0 || yAbs >= bufferHeight) continue;
        float dx = sqrtf((float)radius*radius - (float)yy*yy);
        int xL = (int)floorf(cx - dx);
        int xR = (int)ceilf (cx + dx);
        if (xL < 0) xL = 0;
        if (xR >= bufferWidth) xR = bufferWidth - 1;
        for (int xx = xL; xx <= xR; ++xx)
            pixels[yAbs * bufferWidth + xx] = packed;
    }

    // --- Step 2: antialiased edge ---
    for (int xx = -radius; xx <= radius; ++xx) {
        float dy = sqrtf((float)radius*radius - (float)xx*xx);
        int yi = (int)floorf(dy);
        float f = dy - yi;

        // top edge
        plotAA(cx + xx, cy + yi, 1 - f, packed);
        plotAA(cx + xx, cy + yi + 1, f, packed);

        // bottom edge
        plotAA(cx + xx, cy - yi, 1 - f, packed);
        plotAA(cx + xx, cy - yi - 1, f, packed);
    }

    markDirty(cx - radius, cy - radius, radius * 2, radius * 2);
}

void Window::writeEllipse(int cx, int cy, int rx, int ry, color c) {
    uint32_t packed = (c.r) | (c.g << 8) | (c.b << 16);
    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);

    long rx2 = rx * rx;
    long ry2 = ry * ry;
    long twoRx2 = 2 * rx2;
    long twoRy2 = 2 * ry2;

    long x = 0;
    long y = ry;
    long px = 0;
    long py = twoRx2 * y;

    // Region 1
    long p = round(ry2 - (rx2 * ry) + (0.25 * rx2));
    while (px < py) {
        // draw horizontal spans
        int xL = cx - x, xR = cx + x;
        if (y >= 0 && y < bufferHeight) {
            if (xL < bufferWidth && xR >= 0) {
                for (int xx = fastMax(0, xL); xx <= fastMin(bufferWidth-1, xR); ++xx) {
                    pixels[(cy + y) * bufferWidth + xx] = packed;
                    pixels[(cy - y) * bufferWidth + xx] = packed;
                }
            }
        }
        x++;
        px += twoRy2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= twoRx2;
            p += ry2 + px - py;
        }
    }

    // Region 2
    p = round(ry2 * (x + 0.5) * (x + 0.5) + rx2 * (y - 1) * (y - 1) - rx2 * ry2);
    while (y >= 0) {
        int xL = cx - x, xR = cx + x;
        if (y >= 0 && y < bufferHeight) {
            if (xL < bufferWidth && xR >= 0) {
                for (int xx = fastMax(0, xL); xx <= fastMin(bufferWidth-1, xR); ++xx) {
                    pixels[(cy + y) * bufferWidth + xx] = packed;
                    pixels[(cy - y) * bufferWidth + xx] = packed;
                }
            }
        }
        y--;
        py -= twoRx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twoRy2;
            p += rx2 - py + px;
        }
    }
    markDirty(x, y, rx, ry);
}

void Window::writeChar(int x, int y, WCHAR ch, color c) {
    if (ch > 127) return; // only ASCII supported
    uint32_t packed = (c.r) | (c.g << 8) | (c.b << 16);
    uint32_t* pixels = static_cast<uint32_t*>(pixelBuffer);

    for (int row = 0; row < 8; ++row) {
        uint8_t bits = font8x8_basic[ch][row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1 << col)) {
                int px = x + col;
                int py = y + row;
                if ((unsigned)px < (unsigned)bufferWidth &&
                    (unsigned)py < (unsigned)bufferHeight) {
                    pixels[py * bufferWidth + px] = packed;
                }
            }
        }
    }
    markDirty(x, y, 8, 8);
}

void Window::writeText(int x, int y, const WCHAR* text, color c) {
    int cursorX = x;
    for (const WCHAR* p = text; *p; ++p) {
        writeChar(cursorX, y, *p, c);
        cursorX += 8; // advance 8 pixels per char
    }
}

inline uint32_t blendPixel(uint32_t dst, uint32_t src, uint8_t alpha) {
    uint32_t inv = 255 - alpha;
    uint8_t dr = dst & 0xFF, dg = (dst >> 8) & 0xFF, db = (dst >> 16) & 0xFF;
    uint8_t sr = src & 0xFF, sg = (src >> 8) & 0xFF, sb = (src >> 16) & 0xFF;
    uint8_t r = (sr * alpha + dr * inv) / 255;
    uint8_t g = (sg * alpha + dg * inv) / 255;
    uint8_t b = (sb * alpha + db * inv) / 255;
    return r | (g << 8) | (b << 16);
}

// Blend 4 pixels at once: dst = (src*alpha + dst*(255-alpha)) / 255
inline __m128i blend4_sse2(__m128i dst, __m128i src, __m128i alpha16) {
    // Unpack 8-bit channels to 16-bit
    __m128i dstLo = _mm_unpacklo_epi8(dst, _mm_setzero_si128());
    __m128i dstHi = _mm_unpackhi_epi8(dst, _mm_setzero_si128());
    __m128i srcLo = _mm_unpacklo_epi8(src, _mm_setzero_si128());
    __m128i srcHi = _mm_unpackhi_epi8(src, _mm_setzero_si128());

    __m128i invAlpha = _mm_sub_epi16(_mm_set1_epi16(255), alpha16);

    // Multiply and accumulate
    __m128i lo = _mm_add_epi16(_mm_mullo_epi16(srcLo, alpha16),
                               _mm_mullo_epi16(dstLo, invAlpha));
    __m128i hi = _mm_add_epi16(_mm_mullo_epi16(srcHi, alpha16),
                               _mm_mullo_epi16(dstHi, invAlpha));

    // Divide by 255 (approximate with >>8)
    lo = _mm_srli_epi16(lo, 8);
    hi = _mm_srli_epi16(hi, 8);

    // Pack back to 8-bit
    return _mm_packus_epi16(lo, hi);
}

void Window::writeAlphaBitmap(uint32_t* srcPixels, int srcW, int srcH,
                              int dstX, int dstY, BYTE alpha) {
    if (alpha == 0) return; // fully transparent
    if (alpha == 255) {
        // fast copy path
        int startX = fastMax(0, dstX);
        int startY = fastMax(0, dstY);
        int endX   = fastMin(bufferWidth,  dstX + srcW);
        int endY   = fastMin(bufferHeight, dstY + srcH);

        uint32_t* dst = static_cast<uint32_t*>(pixelBuffer);
        for (int y = startY; y < endY; ++y) {
            int sy = y - dstY;
            memcpy(&dst[y * bufferWidth + startX],
                   &srcPixels[sy * srcW + (startX - dstX)],
                   (endX - startX) * sizeof(uint32_t));
        }
        return;
    }

    int startX = fastMax(0, dstX);
    int startY = fastMax(0, dstY);
    int endX   = fastMin(bufferWidth,  dstX + srcW);
    int endY   = fastMin(bufferHeight, dstY + srcH);

    uint32_t* dst = static_cast<uint32_t*>(pixelBuffer);
    __m128i alpha16 = _mm_set1_epi16(alpha);

    for (int y = startY; y < endY; ++y) {
        int sy = y - dstY;
        uint32_t* dstRow = dst + y * bufferWidth + startX;
        uint32_t* srcRow = srcPixels + sy * srcW + (startX - dstX);

        int count = endX - startX;
        int i = 0;
        for (; i + 4 <= count; i += 4) {
            __m128i d = _mm_loadu_si128((__m128i*)&dstRow[i]);
            __m128i s = _mm_loadu_si128((__m128i*)&srcRow[i]);
            __m128i blended = blend4_sse2(d, s, alpha16);
            _mm_storeu_si128((__m128i*)&dstRow[i], blended);
        }
        for (; i < count; ++i) {
            uint32_t d = dstRow[i];
            uint32_t s = srcRow[i];
            dstRow[i] = blendPixel(d, s, alpha); // scalar fallback
        }
    }
    markDirty(dstX, dstY, srcW, srcH);
}

HBITMAP Window::loadBitmap(const WCHAR* filename, void** outPixels, int* w, int* h) {
    HBITMAP bmp = (HBITMAP)LoadImageW(
        nullptr, filename, IMAGE_BITMAP, 0, 0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION
    );
    if (bmp && outPixels) {
        DIBSECTION ds;
        GetObject(bmp, sizeof(ds), &ds);
        *outPixels = ds.dsBm.bmBits;
        if (w) *w = ds.dsBm.bmWidth;
        if (h) *h = ds.dsBm.bmHeight;
    }
    return bmp;
}

inline void Window::markDirty(int x, int y, int w, int h) {
    if (isAllDirty) return;
    if (!useMarkDirty) return;

    // Reject empty or off-screen rects quickly
    if (w <= 0 || h <= 0) return;
    int rLeft   = x;
    int rTop    = y;
    int rRight  = x + w;
    int rBottom = y + h;

    // Clamp to buffer bounds
    if (rRight <= 0 || rBottom <= 0 || rLeft >= bufferWidth || rTop >= bufferHeight)
        return; // completely outside

    if (!hasDirty) {
        dirtyRect.left   = rLeft;
        dirtyRect.top    = rTop;
        dirtyRect.right  = rRight;
        dirtyRect.bottom = rBottom;
        hasDirty = true;
    } else {
        if (rLeft   < dirtyRect.left)   dirtyRect.left   = rLeft;
        if (rTop    < dirtyRect.top)    dirtyRect.top    = rTop;
        if (rRight  > dirtyRect.right)  dirtyRect.right  = rRight;
        if (rBottom > dirtyRect.bottom) dirtyRect.bottom = rBottom;
    }

    // Final clamp
    if (dirtyRect.left   < 0)            dirtyRect.left   = 0;
    if (dirtyRect.top    < 0)            dirtyRect.top    = 0;
    if (dirtyRect.right  > bufferWidth)  dirtyRect.right  = bufferWidth;
    if (dirtyRect.bottom > bufferHeight) dirtyRect.bottom = bufferHeight;
}

void Window::createBackBuffer(int width, int height) {
    if (width == bufferWidth && height == bufferHeight && backBitmap) return;

    if (backDC && backOldBitmap) {
        SelectObject(backDC, backOldBitmap);
    }
    if (backBitmap) { DeleteObject(backBitmap); backBitmap = nullptr; }
    if (backDC)     { DeleteDC(backDC);         backDC = nullptr; }

    HDC screenDC = GetDC(hwnd);
    backDC = CreateCompatibleDC(screenDC);

    // Fill in the member bmi
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    backBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pixelBuffer, nullptr, 0);
    backOldBitmap = (HBITMAP)SelectObject(backDC, backBitmap);

    bufferWidth  = width;
    bufferHeight = height;

    ReleaseDC(hwnd, screenDC);
}

bool Window::update() {
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> elapsed = now - lastFrame;
    deltaTime = elapsed.count();                // seconds since last frame
    fps = 1.0f / (deltaTime > 0 ? deltaTime : 1);
    lastFrame = now;

    return true; // still running
}

void Window::present() {
    if(useMarkDirty) {
        if (!hasDirty) return; // nothing changed

        HDC hdc = GetDC(hwnd);
        int w = dirtyRect.right - dirtyRect.left;
        int h = dirtyRect.bottom - dirtyRect.top;

        if (w > 0 && h > 0) {
            StretchDIBits(hdc,
                dirtyRect.left, dirtyRect.top, w, h,
                dirtyRect.left, dirtyRect.top, w, h,
                pixelBuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
        }

        ReleaseDC(hwnd, hdc);
        hasDirty = false;
        isAllDirty = false;
    } else {
        HDC hdc = GetDC(hwnd);
        StretchDIBits(hdc,
            0, 0, bufferWidth, bufferHeight,
            0, 0, bufferWidth, bufferHeight,
            pixelBuffer, &bmi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(hwnd, hdc);
    }
}

LRESULT CALLBACK Window::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self && msg != WM_CREATE) {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_SIZE: {
            int width  = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (width > 0 && height > 0) {
                self->createBackBuffer(width, height);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_F11) self->setFullscreen(!self->isFullscreen()); return 0;
        case WM_MOUSEMOVE:
            self->mouseX = (int)(short)LOWORD(lParam);
            self->mouseY = (int)(short)HIWORD(lParam);
            return 0;
        case WM_LBUTTONDOWN: self->leftDown = true; return 0;
        case WM_LBUTTONUP:   self->leftDown = false; return 0;
        case WM_RBUTTONDOWN: self->rightDown = true; return 0;
        case WM_RBUTTONUP:   self->rightDown = false; return 0;
        case WM_MBUTTONDOWN: self->middleDown = true; return 0;
        case WM_MBUTTONUP:   self->middleDown = false; return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MAXIMIZE) { self->setFullscreen(true); return 0; }
            if ((wParam & 0xFFF0) == SC_RESTORE)  { self->setFullscreen(false); return 0; }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            BitBlt(hdc, 0, 0, self->bufferWidth, self->bufferHeight, self->backDC, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Window::setFullscreen(bool enable) {
    if (enable && !fullscreen) {
        // Save current placement
        GetWindowPlacement(hwnd, &prevPlacement);

        // Get monitor info
        HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hmon, &mi);

        // Change style to borderless
        SetWindowLong(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        fullscreen = true;
    }
    else if (!enable && fullscreen) {
        // Restore style
        SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(hwnd, &prevPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        fullscreen = false;
    }
}

#endif