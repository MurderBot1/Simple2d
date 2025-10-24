#ifndef WINDOW_H
#define WINDOW_H

#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <unordered_map>
#include <emmintrin.h>
#include <tmmintrin.h>
#include <bits/algorithmfwd.h>
#include <cmath>
#include <vector>
#include "font8x8/font8x8_basic.h"
#include <algorithm>

struct color {
    unsigned char r, g, b;
};

// #define USE_DIRTY_RECT

// Basic colors
static const struct color Black  = { 0, 0, 0 };
static const struct color White  = { 255, 255, 255 };
static const struct color Grey   = { 128, 128, 128 };
static const struct color Brown  = { 139, 69, 19 };
static const struct color Red    = { 255, 0, 0 };
static const struct color Orange = { 255, 165, 0 };
static const struct color Yellow = { 255, 255, 0 };
static const struct color Green  = { 0, 128, 0 };
static const struct color Blue   = { 0, 0, 255 };
static const struct color Purple = { 128, 0, 128 };

// Grayscale
static const struct color LightGrey   = { 192, 192, 192 };
static const struct color DarkGrey    = { 64, 64, 64 };

// Browns / Earth tones
static const struct color Tan         = { 210, 180, 140 };
static const struct color SandyBrown  = { 244, 164, 96 };
static const struct color DarkBrown   = { 101, 67, 33 };

// Reds / Pinks
static const struct color DarkRed     = { 139, 0, 0 };
static const struct color Crimson     = { 220, 20, 60 };
static const struct color Pink        = { 255, 192, 203 };
static const struct color HotPink     = { 255, 105, 180 };

// Oranges / Yellows
static const struct color Gold        = { 255, 215, 0 };
static const struct color DarkOrange  = { 255, 140, 0 };
static const struct color LightYellow = { 255, 255, 224 };

// Greens
static const struct color LightGreen  = { 144, 238, 144 };
static const struct color Lime        = { 0, 255, 0 };
static const struct color DarkGreen   = { 0, 100, 0 };
static const struct color Teal        = { 0, 128, 128 };

// Blues
static const struct color LightBlue   = { 173, 216, 230 };
static const struct color SkyBlue     = { 135, 206, 235 };
static const struct color Cyan        = { 0, 255, 255 };
static const struct color Navy        = { 0, 0, 128 };

// Purples / Violets
static const struct color Violet      = { 238, 130, 238 };
static const struct color Indigo      = { 75, 0, 130 };
static const struct color Magenta     = { 255, 0, 255 };

#define fastMax(a, b) (a > b) ? a : b
#define fastMin(a, b) (a < b) ? a : b

class Window {
    public:
        Window(HINSTANCE hInst, int width, int height, bool fullscreen);
        ~Window();
        void writeBackground(color c);
        void writePoint(int x, int y, color c);
        void writeLine(int x1, int y1, int x2, int y2, color c);
        void writeSquare(int x, int y, int scale, color c);
        void writeRect(int x1, int y1, int xScale, int yScale, color c);
        void writePolygon(const std::vector<POINT>& pts, color c);
        void plotAA(int x, int y, float c, uint32_t packed);
        void writeCircle(int cx, int cy, int radius, color col);
        void writeEllipse(int x1, int y1, int xScale, int yScale, color c);
        void writeChar(int x, int y, WCHAR ch, color c);
        void writeText(int x, int y, const WCHAR * text, color c);
        void writeAlphaBitmap(uint32_t* srcPixels, int srcW, int srcH, int dstX, int dstY, BYTE alpha);
        HBITMAP loadBitmap(const WCHAR* filename, void** outPixels, int* w, int* h);
        void markDirty(int x, int y, int w, int h);

        inline float getDeltaTime() const { return deltaTime; }
        inline float getFPS() const { return fps; }
        inline bool isFullscreen() const { return fullscreen; }
        inline int getMouseX() const { return mouseX; }
        inline int getMouseY() const { return mouseY; }
        inline void getMousePos(int& x, int& y) const { x = mouseX; y = mouseY; }
        inline bool isLeftDown() const { return leftDown; }
        inline bool isRightDown() const { return rightDown; }
        inline bool isMiddleDown() const { return middleDown; }
        inline int getFrameWidth() const { return bufferWidth; }
        inline int getFrameHeight() const { return bufferHeight; }
        inline void getFrameSize(int& w, int& h) const { w = bufferWidth; h = bufferHeight; }

        void setMarkDirty(bool set) { this->useMarkDirty = set; }
        void createBackBuffer(int width, int height);
        bool update();
        void present();
        void setFullscreen(bool enable);
    private:
        // Window stuff
        static LRESULT CALLBACK windowProc(HWND hwnd, UINT UMsg, WPARAM WParam, LPARAM LParam);
        HWND      hwnd;
        HINSTANCE hInstance;
        bool      running;
        bool      fullscreen;
        HDC backDC = nullptr;
        HBITMAP backBitmap = nullptr;
        HBITMAP backOldBitmap = nullptr;
        HDC imageDC = nullptr;
        void* pixelBuffer = nullptr;
        int lastBkMode = -1;
        int bufferWidth = 0;
        int bufferHeight = 0;
        WINDOWPLACEMENT prevPlacement = { sizeof(prevPlacement) };
        BITMAPINFO bmi = {};

        // Dirty rect
        RECT dirtyRect = {0,0,0,0};
        bool hasDirty = false;
        bool isAllDirty = false;
        bool useMarkDirty = false;
        
        // Mouse stuff
        int mouseX = 0;
        int mouseY = 0;
        bool leftDown   = false;
        bool rightDown  = false;
        bool middleDown = false;

        // FPS
        std::chrono::high_resolution_clock::time_point lastFrame;
        float deltaTime = 0.0f;
        float fps = 0.0f;
};

#endif