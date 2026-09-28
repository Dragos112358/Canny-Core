#ifndef IMAGE_H
#define IMAGE_H

#include <cstdint>
#include <vector>
#include <string>

using namespace std;

struct Image {
    int width;
    int height;
    int channels;
    vector<uint8_t> data;
    
    Image();
    Image(int w, int h, int c);
    
    // Check if image is valid
    bool isValid() const { return !data.empty() && width > 0 && height > 0; }

    uint8_t& at(int x, int y, int c = 0);
    const uint8_t& at(int x, int y, int c = 0) const;

    // Static helpers for kernels
    static vector<float> createGaussianKernel(int size, float sigma);
};

// Thread-safe helper functions accepting startY/endY
void convertToGrayscale(const Image& src, Image& dst, int startY, int endY);
void applyGaussianBlur(const Image& src, Image& dst, const vector<float>& kernel, int kSize, int startY, int endY);
void computeSobel(const Image& src, vector<float>& mag, vector<float>& dir, int startY, int endY);
void computeNonMaxSuppression(int w, int h, const vector<float>& mag, const vector<float>& dir, Image& dst, int startY, int endY);

#endif