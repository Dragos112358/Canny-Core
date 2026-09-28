#include "Image.h"
#include <cmath>
#include <cstring>
#include <omp.h>

Image::Image() {
    this->width = 0;
    this->height = 0;
    this->channels = 0;
    data.clear();
}


Image::Image(int w, int h, int c) {
    this->width = w;
    this->height = h;
    this->channels = c;
    data.resize(w * h * c);
}


uint8_t& Image::at(int x, int y, int c) {
    return data[(y * width + x) * channels + c];
}


const uint8_t& Image::at(int x, int y, int c) const {
    return data[(y * width + x) * channels + c];
}


// parallel Grayscale (OpenMP)
Image Image::toGrayscale(const Image& img) {
    Image gray(img.width, img.height, 1);
    const uint8_t* src = img.data.data();
    uint8_t* dst = gray.data.data();
    int W = img.width;
    int H = img.height;
    int C = img.channels;

    if (C == 1) {
        // fast path: memcpy for single channel
        memcpy(dst, src, W * H);
    } else if (C >= 3) {
        // parallel grayscale conversion
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < W * H; i++) {
            uint8_t r = src[i * C + 0];
            uint8_t g = src[i * C + 1];
            uint8_t b = src[i * C + 2];
            dst[i] = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
        }
    }
    
    return gray;
}


// 1D Gaussian kernel creation
vector<float> Image::createGaussianKernel1D(int size, float sigma) {
    vector<float> kernel(size);
    int half = size / 2;
    float sum = 0.0f;

    // parallel kernel computation with reduction
    #pragma omp parallel for reduction(+:sum) schedule(static)
    for (int i = -half; i <= half; i++) {
        float val = exp(-(i * i) / (2.0f * sigma * sigma));
        kernel[i + half] = val;
        sum += val;
    }

    // normalize
    for (float& v : kernel) {
        v /= sum;
    }

    return kernel;
}


// Gaussian Blur
Image Image::gaussianBlur(const Image& img, int kernelSize, float sigma) {

    if (kernelSize % 2 == 0) kernelSize++; // ensure odd size
    
    int half = kernelSize / 2;
    vector<float> kernel = createGaussianKernel1D(kernelSize, sigma);

    int W = img.width;
    int H = img.height;
    
    Image tmp(W, H, 1);
    Image dst(W, H, 1);
    
    const uint8_t* src = img.data.data();
    uint8_t* tmpPtr = tmp.data.data();
    uint8_t* dstPtr = dst.data.data();

    // HORIZONTAL PASS (X-direction)
    // each row is independent
    #pragma omp parallel for schedule(dynamic)
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float sum = 0.0f;
            
            for (int k = -half; k <= half; k++) {
                int xx = min(max(x + k, 0), W - 1);
                sum += src[y * W + xx] * kernel[k + half];
            }
            
            tmpPtr[y * W + x] = static_cast<uint8_t>(sum);
        }
    }

    // VERTICAL PASS (Y-direction)
    // each column is independent
    #pragma omp parallel for schedule(dynamic)
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float sum = 0.0f;
            
            for (int k = -half; k <= half; k++) {
                int yy = min(max(y + k, 0), H - 1);
                sum += tmpPtr[yy * W + x] * kernel[k + half];
            }
            
            dstPtr[y * W + x] = static_cast<uint8_t>(sum);
        }
    }

    return dst;
}
