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

// GPU kernel to convert image to grayscale
__global__ void toGrayscaleKernel(const uint8_t* rgb, uint8_t* gray, int width, int height, int channels) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        int idx = (y * width + x) * channels;
        
        if(channels >= 3) {
            uint8_t r = rgb[idx];
            uint8_t g = rgb[idx + 1];
            uint8_t b = rgb[idx + 2];
            gray[y * width + x] = static_cast<uint8_t>(0.299f * r + 0.587f * g + 0.114f * b);
        } else {
            gray[y * width + x] = rgb[idx];
        }
    }
}

// GPU kernel to create Gaussian kernel
__global__ void createGaussianKernel(float *kernel, int size, float sigma) {
    // Use shared memory for kernel
    extern __shared__ float sharedKernel[];
    __shared__ float sharedSum;

    // Calculate pixel coordinates
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int idx = y * size + x;
    int halfSize = size / 2;

    if (x < size && y < size) {
        // Use the formula for Gaussian function
        float val = exp(-((x - halfSize) * (x - halfSize) + (y - halfSize) * (y - halfSize)) / (2 * sigma * sigma));
        sharedKernel[idx] = val;
    }

    __syncthreads(); // Ensure all threads have written to shared memory

    if (idx == 0) {
        // Normalize the kernel using one thread
        float sum = 0.0f;
        for (int i = 0; i < size * size; i++) {
            sum += sharedKernel[i];
        }
        sharedSum = sum;
    }

    __syncthreads(); // Ensure sharedSum is ready

    if (x < size && y < size) {
        kernel[idx] = sharedKernel[idx] / sharedSum;
    }
}

// Function to create Gaussian kernel on GPU and return a device pointer
float* createAndGetGaussianKernelGPU(int size, float sigma) {
    float* d_kernel;
    size_t kernelSizeBytes = size * size * sizeof(float);
    cudaMalloc(&d_kernel, kernelSizeBytes);

    // A single block is enough for a small kernel
    dim3 blockSize(size, size);
    dim3 gridSize(1, 1);

    // Set kernelSizeBytes for shared memory
    createGaussianKernel<<<gridSize, blockSize, kernelSizeBytes>>>(d_kernel, size, sigma);
    cudaDeviceSynchronize();

    // Return the device pointer directly
    return d_kernel;
}

// GPU kernel to apply Gaussian Blur
__global__ void gaussianBlurKernel(const uint8_t* input, uint8_t* output, int width, int height, 
                                   const float* kernel, int kernelSize) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        float sum = 0.0f;
        int center = kernelSize / 2;

        for (int ky = -center; ky <= center; ky++) {
            for (int kx = -center; kx <= center; kx++) {
                // Clamp coordinates
                int px = min(max(x + kx, 0), width - 1);
                int py = min(max(y + ky, 0), height - 1);
                
                // Get pixel value
                uint8_t val = input[py * width + px];
                
                // Get kernel value
                float kVal = kernel[(ky + center) * kernelSize + (kx + center)];
                
                sum += val * kVal;
            }
        }
        output[y * width + x] = (uint8_t)(sum);
    }
}
