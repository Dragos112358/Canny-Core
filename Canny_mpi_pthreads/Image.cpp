#include "Image.h"
#include <cmath>
#include <algorithm>
#include <iostream>

Image::Image() : width(0), height(0), channels(0) {}

Image::Image(int w, int h, int c) : width(w), height(h), channels(c) {
    if (w > 0 && h > 0 && c > 0) data.resize(w * h * c);
}

uint8_t& Image::at(int x, int y, int c) {
    return data[(y * width + x) * channels + c];
}

const uint8_t& Image::at(int x, int y, int c) const {
    return data[(y * width + x) * channels + c];
}

vector<float> Image::createGaussianKernel(int size, float sigma) {
    vector<float> kernel(size * size);
    int halfSize = size / 2;
    float sum = 0.0f;

    for(int y = -halfSize; y <= halfSize; y++) {
        for(int x = -halfSize; x <= halfSize; x++) {
            float val = exp(-(x * x + y * y) / (2 * sigma * sigma));
            kernel[(y + halfSize) * size + (x + halfSize)] = val;
            sum += val;
        }
    }
    for(float& v : kernel) v /= sum;
    return kernel;
}

// --- Thread-Safe Processing Functions ---

void convertToGrayscale(const Image& src, Image& dst, int startY, int endY) {
    if (!src.isValid()) return;
    
    // Safe bounds
    int safeEndY = min(endY, src.height);
    int safeStartY = max(0, startY);

    for (int y = safeStartY; y < safeEndY; y++) {
        for (int x = 0; x < src.width; x++) {
            if (src.channels == 1) {
                dst.at(x, y) = src.at(x, y);
            } else if (src.channels >= 3) {
                uint8_t r = src.at(x, y, 0);
                uint8_t g = src.at(x, y, 1);
                uint8_t b = src.at(x, y, 2);
                dst.at(x, y) = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
            }
        }
    }
}

void applyGaussianBlur(const Image& src, Image& dst, const vector<float>& kernel, int kSize, int startY, int endY) {
    if (!src.isValid()) return;
    int center = kSize / 2;
    int safeEndY = min(endY, src.height);
    int safeStartY = max(0, startY);

    for (int y = safeStartY; y < safeEndY; y++) {
        for (int x = 0; x < src.width; x++) {
            float sum = 0.0f;
            for (int ky = -center; ky <= center; ky++) {
                for (int kx = -center; kx <= center; kx++) {
                    int px = min(max(x + kx, 0), src.width - 1);
                    int py = min(max(y + ky, 0), src.height - 1);
                    sum += src.at(px, py) * kernel[(ky + center) * kSize + (kx + center)];
                }
            }
            dst.at(x, y) = static_cast<uint8_t>(sum);
        }
    }
}

void computeSobel(const Image& src, vector<float>& magnitude, vector<float>& direction, int startY, int endY) {
    if (!src.isValid()) return;
    const int Gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    const int Gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};

    int safeEndY = min(endY, src.height);
    int safeStartY = max(0, startY);

    for (int y = safeStartY; y < safeEndY; y++) {
        for (int x = 1; x < src.width - 1; x++) {
            // Boundary check for convolution
            if (y <= 0 || y >= src.height - 1) continue;

            float gx = 0.0f, gy = 0.0f;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    uint8_t pixel = src.at(x + kx, y + ky);
                    gx += pixel * Gx[ky + 1][kx + 1];
                    gy += pixel * Gy[ky + 1][kx + 1];
                }
            }
            int idx = y * src.width + x;
            magnitude[idx] = sqrtf(gx * gx + gy * gy);
            direction[idx] = atan2f(gy, gx);
        }
    }
}

void computeNonMaxSuppression(int width, int height, const vector<float>& magnitude, const vector<float>& direction, Image& dst, int startY, int endY) {
    const float PI = 3.14159265f;
    int safeEndY = min(endY, height);
    int safeStartY = max(0, startY);

    for (int y = safeStartY; y < safeEndY; y++) {
        if (y <= 0 || y >= height - 1) continue;
        
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float mag = magnitude[idx];
            float angle = direction[idx] * 180.0f / PI;
            if (angle < 0) angle += 180.0f;

            float n1 = 0, n2 = 0;
            if ((angle >= 0 && angle < 22.5) || (angle >= 157.5 && angle < 180)) {
                n1 = magnitude[idx - 1]; n2 = magnitude[idx + 1];
            } else if (angle >= 22.5 && angle < 67.5) {
                n1 = magnitude[idx - width + 1]; n2 = magnitude[idx + width - 1];
            } else if (angle >= 67.5 && angle < 112.5) {
                n1 = magnitude[idx - width]; n2 = magnitude[idx + width];
            } else {
                n1 = magnitude[idx - width - 1]; n2 = magnitude[idx + width + 1];
            }

            if (mag >= n1 && mag >= n2)
                dst.at(x, y) = static_cast<uint8_t>(min(mag, 255.0f));
            else
                dst.at(x, y) = 0;
        }
    }
}