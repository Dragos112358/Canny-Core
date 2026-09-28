#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <chrono>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace std;

// Sobel gradient function to compute magnitude and direction
__global__ void sobelKernel(const uint8_t* img, float* magnitude, float* direction, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= 1 && x < width - 1 && y >= 1 && y < height - 1) {
        float gx = 0.0f;
        float gy = 0.0f;

        // Sobel kernels
        const int Gx[3][3] = {
            {-1, 0, 1},
            {-2, 0, 2},
            {-1, 0, 1}
        };

        const int Gy[3][3] = {
            {-1, -2, -1},
            {0, 0, 0},
            {1, 2, 1}
        };

        for (int ky = -1; ky <= 1; ky++) {
            for (int kx = -1; kx <= 1; kx++) {
                uint8_t pixel = img[(y + ky) * width + (x + kx)];
                gx += pixel * Gx[ky + 1][kx + 1];
                gy += pixel * Gy[ky + 1][kx + 1];
            }
        }
                
        int idx = y * width + x;
        magnitude[idx] = sqrtf(gx * gx + gy * gy);
        direction[idx] = atan2f(gy, gx);
    }
}

// Non-Maximum Suppression Kernel
__global__ void nonMaxSuppressionKernel(const float* magnitude, const float* direction, uint8_t* output, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= 1 && x < width - 1 && y >= 1 && y < height - 1) {
        int idx = y * width + x;
        float mag = magnitude[idx];
        float angle = direction[idx] * 180.0f / M_PI;
        
        // Normalize angle to [0, 180)
        if (angle < 0)
            angle += 180.0f;

        float neighbor1 = 0, neighbor2 = 0;

        // Check neighbors in the gradient direction
        if ((angle >= 0 && angle < 22.5) || (angle >= 157.5 && angle < 180)) {
            neighbor1 = magnitude[idx - 1];
            neighbor2 = magnitude[idx + 1];
        } else if (angle >= 22.5 && angle < 67.5) {
            neighbor1 = magnitude[idx - width + 1];
            neighbor2 = magnitude[idx + width - 1];
        } else if (angle >= 67.5 && angle < 112.5) {
            neighbor1 = magnitude[idx - width];
            neighbor2 = magnitude[idx + width];
        } else if (angle >= 112.5 && angle < 157.5) {
            neighbor1 = magnitude[idx - width - 1];
            neighbor2 = magnitude[idx + width + 1];
        }

        if (mag >= neighbor1 && mag >= neighbor2)
            output[idx] = (uint8_t)(min(mag, 255.0f));
        else
            output[idx] = 0;
    }
}

// Hysteresis Thresholding Kernel
__global__ void hysteresisThresholdKernel(const uint8_t* input, uint8_t* output, int width, int height, int low, int high) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        int idx = y * width + x;
        uint8_t val = input[idx];

        if (val >= high) {
            output[idx] = 255; // Strong
        } else if (val >= low) {
            output[idx] = 128; // Weak
        } else {
            output[idx] = 0;   // Suppressed
        }
    }
}

// Hysteresis Edge Tracking Kernel
__global__ void hysteresisIterativeKernel(uint8_t* img, int width, int height, bool* changed) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= 1 && x < width - 1 && y >= 1 && y < height - 1) {
        int idx = y * width + x;
        
        if (img[idx] == 128) { // Weak edge
            bool hasStrongNeighbor = false;
            
            // Check 8 neighbors
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    if (kx == 0 && ky == 0) continue;
                    if (img[(y + ky) * width + (x + kx)] == 255) {
                        hasStrongNeighbor = true;
                        break;
                    }
                }
                if(hasStrongNeighbor) break;
            }

            if (hasStrongNeighbor) {
                img[idx] = 255;
                *changed = true; // Indicate that a change was made
            }
        }
    }
}

// Hysteresis Cleanup Kernel
__global__ void hysteresisCleanupKernel(uint8_t* img, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        if (img[y * width + x] == 128) {
            img[y * width + x] = 0;
        }
    }
}

// Pipeline function to perform Canny Edge Detection
uint8_t* cannyEdgeDetection(u_int8_t* input, int width, int height, int channels, int lowThreshold,
                            int highThreshold, int kernelSize, float sigma, int blockSizeValue) {
    double t_alloc = 0.0, t_h2d = 0.0, t_gray = 0.0, t_blur = 0.0;
    double t_sobel = 0.0, t_nms = 0.0, t_hyst = 0.0, t_d2h = 0.0;

    auto start_total = chrono::high_resolution_clock::now();
    auto t1 = chrono::high_resolution_clock::now();
    
    uint8_t *d_rgb, *d_gray, *d_blurred, *d_edges;
    float *d_mag, *d_dir, *d_kernel;

    int numPixels = width * height;

    cudaMalloc(&d_rgb, numPixels * channels);
    cudaMalloc(&d_gray, numPixels);
    cudaMalloc(&d_blurred, numPixels);
    cudaMalloc(&d_mag, numPixels * sizeof(float));
    cudaMalloc(&d_dir, numPixels * sizeof(float));
    cudaMalloc(&d_edges, numPixels);
    
    auto t2 = chrono::high_resolution_clock::now();
    t_alloc = chrono::duration<double>(t2 - t1).count();

    // --- 2. TRANSFER HOST -> DEVICE ---
    t1 = chrono::high_resolution_clock::now();
    cudaMemcpy(d_rgb, input, numPixels * channels, cudaMemcpyHostToDevice);
    t2 = chrono::high_resolution_clock::now();
    t_h2d = chrono::duration<double>(t2 - t1).count();

    // Calculate Grid/Block sizes
    dim3 blockSize(blockSizeValue, blockSizeValue);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, (height + blockSize.y - 1) / blockSize.y);

    cout << "Running CUDA Canny pipeline...\n";
    
    // --- 3. GRAYSCALE ---
    t1 = chrono::high_resolution_clock::now();
    toGrayscaleKernel<<<gridSize, blockSize>>>(d_rgb, d_gray, width, height, channels);
    cudaDeviceSynchronize(); // Obligatoriu pentru timing corect
    t2 = chrono::high_resolution_clock::now();
    t_gray = chrono::duration<double>(t2 - t1).count();
    
    // --- 4. GAUSSIAN BLUR ---
    t1 = chrono::high_resolution_clock::now();
    d_kernel = createAndGetGaussianKernelGPU(kernelSize, sigma); 
    gaussianBlurKernel<<<gridSize, blockSize>>>(d_gray, d_blurred, width, height, d_kernel, kernelSize);
    cudaDeviceSynchronize();
    t2 = chrono::high_resolution_clock::now();
    t_blur = chrono::duration<double>(t2 - t1).count();

    // --- 5. SOBEL GRADIENTS ---
    t1 = chrono::high_resolution_clock::now();
    sobelKernel<<<gridSize, blockSize>>>(d_blurred, d_mag, d_dir, width, height);
    cudaDeviceSynchronize();
    t2 = chrono::high_resolution_clock::now();
    t_sobel = chrono::duration<double>(t2 - t1).count();

    // --- 6. NON-MAXIMUM SUPPRESSION ---
    t1 = chrono::high_resolution_clock::now();
    nonMaxSuppressionKernel<<<gridSize, blockSize>>>(d_mag, d_dir, d_edges, width, height);
    cudaDeviceSynchronize();
    t2 = chrono::high_resolution_clock::now();
    t_nms = chrono::duration<double>(t2 - t1).count();
    
    // --- 7. HYSTERESIS (Tot procesul: Threshold + Loop + Cleanup) ---
    t1 = chrono::high_resolution_clock::now();
    
    // 7a. Threshold
    hysteresisThresholdKernel<<<gridSize, blockSize>>>(d_edges, d_blurred, width, height, lowThreshold, highThreshold); 
    cudaDeviceSynchronize();

    // 7b. Iterative Tracking
    bool h_changed = true;
    bool* d_changed;
    cudaMalloc(&d_changed, sizeof(bool));

    int iterations = 0;
    while (h_changed) {
        h_changed = false;
        cudaMemcpy(d_changed, &h_changed, sizeof(bool), cudaMemcpyHostToDevice);

        hysteresisIterativeKernel<<<gridSize, blockSize>>>(d_blurred, width, height, d_changed);
        cudaDeviceSynchronize();
        
        cudaMemcpy(&h_changed, d_changed, sizeof(bool), cudaMemcpyDeviceToHost);
        iterations++;
        
        if (iterations > 1000) break;
    }
    
    // 7c. Cleanup
    hysteresisCleanupKernel<<<gridSize, blockSize>>>(d_blurred, width, height);
    cudaDeviceSynchronize();

    t2 = chrono::high_resolution_clock::now();
    t_hyst = chrono::duration<double>(t2 - t1).count();

    cout << "Hysteresis converged in " << iterations << " iterations.\n";

    // --- 8. TRANSFER DEVICE -> HOST ---
    t1 = chrono::high_resolution_clock::now();
    uint8_t* resultData = new uint8_t[numPixels];
    cudaMemcpy(resultData, d_blurred, numPixels, cudaMemcpyDeviceToHost);
    t2 = chrono::high_resolution_clock::now();
    t_d2h = chrono::duration<double>(t2 - t1).count();

    // Cleanup device memory
    cudaFree(d_rgb); cudaFree(d_gray); cudaFree(d_blurred);
    cudaFree(d_mag); cudaFree(d_dir); cudaFree(d_edges);
    cudaFree(d_kernel); cudaFree(d_changed);
    
    // --- TIME BREAKDOWN  ---
    cout << "\n----------------------------------------\n";
    cout << "CUDA Execution Breakdown:\n";
    cout << "  Alloc GPU Mem:   " << fixed << t_alloc << " s\n";
    cout << "  Memcpy H->D:     " << t_h2d << " s\n";
    cout << "  Grayscale:       " << t_gray << " s\n";
    cout << "  Gaussian Blur:   " << t_blur << " s\n";
    cout << "  Sobel:           " << t_sobel << " s\n";
    cout << "  NMS:             " << t_nms << " s\n";
    cout << "  Hysteresis:      " << t_hyst << " s (" << iterations << " iters)\n";
    cout << "  Memcpy D->H:     " << t_d2h << " s\n";
    cout << "----------------------------------------\n";
    
    double t_kernels_only = t_gray + t_blur + t_sobel + t_nms + t_hyst;
    cout << "Total Kernel Time: " << t_kernels_only << " s\n";
    cout << "Total GPU Time:    " << (t_alloc + t_h2d + t_kernels_only + t_d2h) << " s\n";
    cout << "----------------------------------------\n\n";

    return resultData;
}

// Function to print usage instructions
void printUsage(const char* programName) {
    cout << "Usage: " << programName << " <input_image> [output_image.png] [low_threshold]"
                                            " [high_threshold] [kernel_size] [sigma] [block_size]\n";
    cout << "\nArguments:\n";
    cout << "  input_image       - Path to input image (jpg, png, bmp, tga, etc.)\n";
    cout << "  output_image.png  - (Optional) Output PNG file (default: output_canny.png)\n";
    cout << "  low_threshold     - (Optional) Lower threshold (default: 15)\n";
    cout << "  high_threshold    - (Optional) Upper threshold (default: 40)\n";
    cout << "  kernel_size       - (Optional) Gaussian kernel size (default: 9)\n";
    cout << "  sigma             - (Optional) Gaussian sigma (default: 1.5)\n";
    cout << "  block_size        - (Optional) CUDA block size (default: 16)\n";
}


int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return -1;
    }
    
    // Parse arguments
    string inputPath = argv[1];
    string outputPath = (argc >= 3) ? argv[2] : "output_canny.png";

    int lowThreshold = 15;
    int highThreshold = 40;
    int kernelSize = 9;
    int blockSizeValue = 16;
    float sigma = 1.5f;

    for(int i = 3; i < argc; i++) {
        switch(i) {
            case 3: lowThreshold = stoi(argv[i]); break;
            case 4: highThreshold = stoi(argv[i]); break;
            case 5: kernelSize = stoi(argv[i]); break;
            case 6: sigma = stof(argv[i]); break;
            case 7: blockSizeValue = stoi(argv[i]); break;
            default: break;
        }
    }
    
    if (lowThreshold < 0 || highThreshold < 0 || lowThreshold >= highThreshold) {
        cerr << "Error: Invalid thresholds. Ensure 0 <= low < high\n";
        return -1;
    }

    if(kernelSize <= 0 || kernelSize % 2 == 0) {
        cerr << "Error: Kernel size must be positive and odd.\n";
        return -1;
    }

    if(blockSizeValue <= 0) {
        cerr << "Error: Block size must be positive.\n";
        return -1;
    }

    // Load image
    cout << "Loading image...\n";
    int width, height, channels;
    uint8_t* h_rgb = stbi_load(inputPath.c_str(), &width, &height, &channels, 0);

    if (!h_rgb) {
        cerr << "Error: Could not load image '" << inputPath << "'\n";
        cerr << "   Reason: " << stbi_failure_reason() << "\n";
        return -1;
    }
    cout << "Image loaded: " << width << "x" << height << " pixels, " << channels << " channels\n\n";

    auto start = chrono::high_resolution_clock::now();

    // Apply Canny Edge Detection using CUDA
    uint8_t* edges = cannyEdgeDetection(h_rgb, width, height, channels, lowThreshold, highThreshold,
                            kernelSize, sigma, blockSizeValue);

    auto end = chrono::high_resolution_clock::now();
    auto elapsed = chrono::duration<double>(end - start).count();

    cout << "Elapsed time: " << elapsed << " seconds\n";

    cout << "\nSaving result to '" << outputPath << "'...\n";

    if(!stbi_write_png(outputPath.c_str(), width, height, 1, edges, width)) {
        cerr << "Error: Could not save image to '" << outputPath << "'\n";
        stbi_image_free(h_rgb);
        return -1;
    }

    cout << "Success! Edge detection completed.\n";

    // Statistics
    int totalPixels = width * height;
    int edgePixels = 0;
    for (int i = 0; i < totalPixels; i++) {
        if (edges[i] == 255) edgePixels++;
    }

    double edgePercentage = (double)edgePixels / totalPixels * 100.0;
    
    cout << "\nStatistics:\n";
    cout << "   Total pixels: " << totalPixels << "\n";
    cout << "   Edge pixels:  " << edgePixels << " (" << fixed << edgePercentage << "%)\n";

    // Cleanup
    stbi_image_free(h_rgb);
    delete[] edges;

    return 0;
}