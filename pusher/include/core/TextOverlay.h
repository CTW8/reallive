#pragma once

#include <cstdint>
#include <cstring>
#include <ctime>

namespace reallive {

// Draws a "YYYY-MM-DD HH:MM:SS" timestamp directly on NV12 frame data.
// Uses a built-in 8x16 bitmap font, rendered at 2x scale for visibility.
class TextOverlay {
public:
    // Draw current timestamp on NV12 frame, bottom-right corner.
    // data: raw NV12 buffer (Y plane + UV plane)
    // width/height: frame dimensions
    static void drawTimestamp(uint8_t* data, int width, int height) {
        time_t now = time(nullptr);
        struct tm* lt = localtime(&now);
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                 lt->tm_hour, lt->tm_min, lt->tm_sec);

        const int scale = 2;
        const int charW = GLYPH_W * scale;   // 16
        const int charH = GLYPH_H * scale;   // 32
        const int textLen = static_cast<int>(strlen(buf));
        const int padX = 8;
        const int padY = 6;
        const int boxW = textLen * charW + padX * 2;
        const int boxH = charH + padY * 2;

        // Position: bottom-right with margin
        const int margin = 16;
        const int boxX = width - boxW - margin;
        const int boxY = height - boxH - margin;

        if (boxX < 0 || boxY < 0) return;

        // Draw semi-transparent dark background on Y plane
        uint8_t* yPlane = data;
        for (int y = boxY; y < boxY + boxH && y < height; y++) {
            for (int x = boxX; x < boxX + boxW && x < width; x++) {
                yPlane[y * width + x] = yPlane[y * width + x] / 4;
            }
        }

        // Desaturate the background area in UV plane
        uint8_t* uvPlane = data + width * height;
        int uvStartY = boxY / 2;
        int uvEndY = (boxY + boxH + 1) / 2;
        int uvStartX = (boxX / 2) * 2;  // align to even pixel
        int uvEndX = ((boxX + boxW + 1) / 2) * 2;
        for (int y = uvStartY; y < uvEndY && y < height / 2; y++) {
            for (int x = uvStartX; x < uvEndX && x < width; x += 2) {
                int idx = y * width + x;
                // Blend U,V towards neutral (128)
                uvPlane[idx]     = static_cast<uint8_t>((uvPlane[idx] + 128 * 3) / 4);
                uvPlane[idx + 1] = static_cast<uint8_t>((uvPlane[idx + 1] + 128 * 3) / 4);
            }
        }

        // Draw each character
        int textX = boxX + padX;
        int textY = boxY + padY;
        for (int i = 0; i < textLen; i++) {
            drawChar(yPlane, width, height, textX + i * charW, textY, buf[i], scale);
        }
    }

    static void drawBoundingBox(
        uint8_t* data,
        int width,
        int height,
        int x,
        int y,
        int w,
        int h,
        int thickness = 3
    ) {
        if (!data || width <= 0 || height <= 0 || w <= 0 || h <= 0) return;
        if (thickness < 1) thickness = 1;

        int x0 = x;
        int y0 = y;
        int x1 = x + w - 1;
        int y1 = y + h - 1;

        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= width) x1 = width - 1;
        if (y1 >= height) y1 = height - 1;
        if (x1 <= x0 || y1 <= y0) return;

        uint8_t* yPlane = data;
        uint8_t* uvPlane = data + width * height;

        const uint8_t yValue = 96;
        const uint8_t uValue = 84;
        const uint8_t vValue = 255;

        auto paintY = [&](int px, int py) {
            if (px < 0 || py < 0 || px >= width || py >= height) return;
            yPlane[py * width + px] = yValue;
        };

        auto paintUv = [&](int px, int py) {
            if (px < 0 || py < 0 || px >= width || py >= height) return;
            int uvY = py / 2;
            int uvX = (px / 2) * 2;
            int idx = uvY * width + uvX;
            uvPlane[idx] = uValue;
            uvPlane[idx + 1] = vValue;
        };

        for (int t = 0; t < thickness; t++) {
            const int ty0 = y0 + t;
            const int ty1 = y1 - t;
            for (int px = x0; px <= x1; px++) {
                paintY(px, ty0);
                paintY(px, ty1);
                paintUv(px, ty0);
                paintUv(px, ty1);
            }
        }

        for (int t = 0; t < thickness; t++) {
            const int tx0 = x0 + t;
            const int tx1 = x1 - t;
            for (int py = y0; py <= y1; py++) {
                paintY(tx0, py);
                paintY(tx1, py);
                paintUv(tx0, py);
                paintUv(tx1, py);
            }
        }
    }

private:
    static constexpr int GLYPH_W = 8;
    static constexpr int GLYPH_H = 16;

    // Map ASCII char to font table index
    static int glyphIndex(char c) {
        if (c >= '0' && c <= '9') return 2 + (c - '0');
        if (c == '-') return 1;
        if (c == ':') return 12;
        return 0; // space and anything else
    }

    // Draw a single character on Y plane at 2x scale
    static void drawChar(uint8_t* yPlane, int w, int h,
                         int x0, int y0, char c, int scale) {
        int idx = glyphIndex(c);
        const uint8_t* glyph = FONT[idx];

        for (int row = 0; row < GLYPH_H; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < GLYPH_W; col++) {
                if (bits & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = x0 + col * scale + sx;
                            int py = y0 + row * scale + sy;
                            if (px >= 0 && px < w && py >= 0 && py < h) {
                                yPlane[py * w + px] = 235; // white (video range)
                            }
                        }
                    }
                }
            }
        }
    }

    // 8x16 bitmap font: ' ', '-', '0'-'9', ':'
    // Standard VGA/CP437 ROM font glyphs (public domain)
    static constexpr uint8_t FONT[13][16] = {
        // [0] ' '
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        // [1] '-'
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,
         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        // [2] '0'
        {0x00,0x00,0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,
         0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
        // [3] '1'
        {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,
         0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
        // [4] '2'
        {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,
         0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00},
        // [5] '3'
        {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,
         0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
        // [6] '4'
        {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,
         0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00},
        // [7] '5'
        {0x00,0x00,0xFE,0xC0,0xC0,0xFC,0x06,0x06,
         0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
        // [8] '6'
        {0x00,0x00,0x38,0x60,0xC0,0xFC,0xC6,0xC6,
         0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
        // [9] '7'
        {0x00,0x00,0xFE,0xC6,0x06,0x0C,0x18,0x30,
         0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00},
        // [10] '8'
        {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,
         0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00},
        // [11] '9'
        {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,
         0x06,0x0C,0x78,0x00,0x00,0x00,0x00,0x00},
        // [12] ':'
        {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,
         0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
    };
};

} // namespace reallive
