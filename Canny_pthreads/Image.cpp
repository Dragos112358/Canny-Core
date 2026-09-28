#include "Image.h"
#include <cmath>
#include <algorithm>
#include <pthread.h>
using namespace std;


// ============================================================================
// CONSTRUCTORS
// ============================================================================
uint8_t& Image::at(int x, int y, int c) {
    return data[(y * width + x) * channels + c];
}

const uint8_t& Image::at(int x, int y, int c) const {
    return data[(y * width + x) * channels + c];
}

Image::Image() {
    width = 0;
    height = 0;
    channels = 0;
    data.clear();
}

Image::Image(int w, int h, int c) : width(w), height(h), channels(c) {
    data.resize(w * h * c);
}

// ============================================================================
// GRAYSCALE
// ============================================================================

vector<float> createGaussianKernel(int size, float sigma) {
    vector<float> kernel(size * size);
    int halfSize = size / 2;
    float sum = 0.0f;

    for(int y = -halfSize; y <= halfSize; y++) {
        for(int x = -halfSize; x <= halfSize; x++) {
            float val = exp(-(x * x + y * y) / (2 * sigma * sigma));
            int idx = (y + halfSize) * size + (x + halfSize);
            kernel[idx] = val;
            sum += val;
        }
    }

    // Normalize kernel
    for(float& v : kernel) {
        v /= sum;
    }

    return kernel;
}

void* threadGrayscale(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    int width = args->input->width;
    int height = args->input->height;
    int channels = args->input->channels;
    
    int hStart = args->id * height / args->totalThreads;
    int hEnd = (args->id + 1) * height / args->totalThreads;

    const uint8_t* inData = args->input->data.data();
    uint8_t* outData = args->output->data.data();

    if (channels == 1) {
        // Direct copy for grayscale input
        int startIdx = hStart * width;
        int endIdx = hEnd * width;
        copy(inData + startIdx, inData + endIdx, outData + startIdx);
    } else {
        // RGB to grayscale conversion with optimized coefficients
        for (int y = hStart; y < hEnd; y++) {
            int outRowIdx = y * width;
            int inRowIdx = outRowIdx * channels;
            
            for (int x = 0; x < width; x++) {
                int inIdx = inRowIdx + x * channels;
                
                // Pre-multiply and use integer math for speed
                int gray = (77 * inData[inIdx] + 150 * inData[inIdx + 1] + 29 * inData[inIdx + 2]) >> 8;
                outData[outRowIdx + x] = static_cast<uint8_t>(gray);
            }
        }
    }
    pthread_exit(nullptr);
}

Image toGrayscalePthreads(const Image& img, int numThreads) {
    Image gray(img.width, img.height, 1);
    pthread_t threads[numThreads];
    ThreadArgs args[numThreads];
    
    for (int i = 0; i < numThreads; i++) {
        args[i] = {i, numThreads, &img, &gray};
        pthread_create(&threads[i], nullptr, threadGrayscale, &args[i]);
    }
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    return gray;
}

// ============================================================================
// GAUSSIAN KERNEL
// ============================================================================

vector<float> Image::createGaussianKernel(int size, float sigma) {
    vector<float> kernel(size * size);
    int half = size / 2;
    float sum = 0.0f;

    for (int y = -half; y <= half; y++) {
        for (int x = -half; x <= half; x++) {

            float value = exp(-(x * x + y * y) / (2 * sigma * sigma));
            kernel[(y + half) * size + (x + half)] = value;
            sum += value;
        }
    }

    // Normalize
    for (float& v : kernel) v /= sum;

    return kernel;
}

// ============================================================================
// GAUSSIAN BLUR (variable kernel)
// ============================================================================

void* threadBlur(void* arg) {
    BlurArgs* args = (BlurArgs*)arg;
    int width = args->input->width;
    int height = args->input->height;
    int kernelSize = args->kernelSize;
    int center = kernelSize / 2;
    
    int hStart = args->id * height / args->totalThreads;
    int hEnd = (args->id + 1) * height / args->totalThreads;
    
    const uint8_t* inData = args->input->data.data();
    uint8_t* outData = args->output->data.data();
    const float* kernel = args->kernel->data();

    // Cache-friendly processing
    for (int y = hStart; y < hEnd; y++) {
        int outRowIdx = y * width;
        
        for (int x = 0; x < width; x++) {
            float sum = 0.0f;
            
            // Unrolled inner loops for better performance
            for (int ky = -center; ky <= center; ky++) {
                int py = min(max(y + ky, 0), height - 1);
                const uint8_t* inRow = inData + py * width;
                const float* kernelRow = kernel + (ky + center) * kernelSize;
                
                for (int kx = -center; kx <= center; kx++) {
                    int px = min(max(x + kx, 0), width - 1);
                    sum += inRow[px] * kernelRow[kx + center];
                }
            }
            
            outData[outRowIdx + x] = static_cast<uint8_t>(sum);
        }
    }
    pthread_exit(nullptr);
}

Image gaussianBlurPthreads(const Image& img, int kernelSize, float sigma, int numThreads) {
    // Ensure odd kernel size
    if (kernelSize % 2 == 0) kernelSize++;
    
    // Generate Gaussian kernel
    vector<float> kernel = createGaussianKernel(kernelSize, sigma);
    
    Image blurred(img.width, img.height, 1);
    pthread_t threads[numThreads];
    BlurArgs args[numThreads];
    
    for (int i = 0; i < numThreads; i++) {
        args[i] = {i, numThreads, &img, &blurred, &kernel, kernelSize};
        pthread_create(&threads[i], nullptr, threadBlur, &args[i]);
    }
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    return blurred;
}
