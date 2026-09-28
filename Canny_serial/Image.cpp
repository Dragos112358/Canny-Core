#include "Image.h"
#include <cmath>

Image::Image() {
    this->width = 0;
    this->height = 0;
    this->channels = 0;
    data.clear();
}

// Parameterized constructor
Image::Image(int w, int h, int c) {
    this->width = w;
    this->height = h;
    this->channels = c;
    data.resize(w * h * c);
}

// Non-const accessor
uint8_t& Image::at(int x, int y, int c) {
    return data[(y * width + x) * channels + c];
}

// Const accessor
const uint8_t& Image::at(int x, int y, int c) const {
    return data[(y * width + x) * channels + c];
}

// Function to convert to Grayscale
Image Image::toGrayscale(const Image& img) {
    Image gray(img.width, img.height, 1);
    
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            if (img.channels == 1) {
                gray.at(x, y) = img.at(x, y);
            } else if (img.channels >= 3) {
                // Luminosity: 0.299*R + 0.587*G + 0.114*B
                uint8_t r = img.at(x, y, 0);
                uint8_t g = img.at(x, y, 1);
                uint8_t b = img.at(x, y, 2);
                gray.at(x, y) = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
            }
        }
    }
    
    return gray;
}

// Function to create a Gaussian kernel
vector<float> Image::createGaussianKernel(int size, float sigma) {
    vector<float> kernel(size * size);
    int halfSize = size / 2, idx;
    float sum = 0.0f, val;

    for(int y = -halfSize; y <= halfSize; y++) {
        for(int x = -halfSize; x <= halfSize; x++) {
            // Use the formula for Gaussian function
            val = exp(-(x * x + y * y) / (2 * sigma * sigma));
            idx = (y + halfSize) * size + (x + halfSize);
            kernel[idx] = val;
            sum += val;
        }
    }

    for(float& v : kernel) {
        v /= sum; // Normalize the kernel
    }

    return kernel;
}

// Function to apply Gaussian Blur
Image Image::gaussianBlur(const Image& img, int kernelSize, float sigma) {
    // Kernel Gaussian
    if (kernelSize % 2 == 0) kernelSize++; // Ensure odd size
    
    int center = kernelSize / 2;
    vector<float> kernel = createGaussianKernel(kernelSize, sigma);

	Image blurred(img.width, img.height, 1);
    
    // Apply Gaussian Blur
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            float sum = 0.0f;
            
            for (int ky = -center; ky <= center; ky++) {
                for (int kx = -center; kx <= center; kx++) {
                    int px = min(max(x + kx, 0), img.width - 1);
                    int py = min(max(y + ky, 0), img.height - 1);

                    sum += img.at(px, py) * kernel[(ky + center) * kernelSize + (kx + center)];
                }
            }
            
            blurred.at(x, y) = static_cast<uint8_t>(sum);
        }
    }
    
    return blurred;
}


