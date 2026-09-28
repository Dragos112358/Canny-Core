#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <chrono>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <omp.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <cstring>

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
// Pipeline function to perform Canny Edge Detection
uint8_t* cannyEdgeDetection(u_int8_t* input, int width, int height, int channels, int lowThreshold,
                            int highThreshold, int kernelSize, float sigma, int blockSizeValue) {
    
    // Timing variables
    double t_alloc = 0.0, t_h2d = 0.0, t_gray = 0.0, t_blur = 0.0;
    double t_sobel = 0.0, t_nms = 0.0, t_hyst = 0.0, t_d2h = 0.0;

    // --- 1. MEMORY ALLOCATION ---
    auto t1 = chrono::high_resolution_clock::now();

    uint8_t *d_rgb, *d_gray, *d_blurred, *d_edges;
    float *d_mag, *d_dir, *d_kernel;

    int numPixels = width * height;

    // Device memory allocation
    cudaMalloc(&d_rgb, numPixels * channels);
    cudaMalloc(&d_gray, numPixels);
    cudaMalloc(&d_blurred, numPixels);
    cudaMalloc(&d_mag, numPixels * sizeof(float));
    cudaMalloc(&d_dir, numPixels * sizeof(float));
    cudaMalloc(&d_edges, numPixels);

    // No sync needed for malloc, it is blocking on the default stream anyway
    auto t2 = chrono::high_resolution_clock::now();
    t_alloc = chrono::duration<double>(t2 - t1).count();

    // --- 2. HOST -> DEVICE TRANSFER ---
    t1 = chrono::high_resolution_clock::now();
    cudaMemcpy(d_rgb, input, numPixels * channels, cudaMemcpyHostToDevice);
    t2 = chrono::high_resolution_clock::now();
    t_h2d = chrono::duration<double>(t2 - t1).count();

    // Launch configuration
    dim3 blockSize(blockSizeValue, blockSizeValue);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x, (height + blockSize.y - 1) / blockSize.y);

    // --- 3. GRAYSCALE CONVERSION ---
    t1 = chrono::high_resolution_clock::now();
    toGrayscaleKernel<<<gridSize, blockSize>>>(d_rgb, d_gray, width, height, channels);
    cudaDeviceSynchronize(); // Sync required for accurate timing measurement
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
    
    // --- 7. HYSTERESIS THRESHOLDING ---
    t1 = chrono::high_resolution_clock::now();
    hysteresisThresholdKernel<<<gridSize, blockSize>>>(d_edges, d_blurred, width, height, lowThreshold, highThreshold); 
    cudaDeviceSynchronize();

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

    hysteresisCleanupKernel<<<gridSize, blockSize>>>(d_blurred, width, height);
    cudaDeviceSynchronize();

    t2 = chrono::high_resolution_clock::now();
    t_hyst = chrono::duration<double>(t2 - t1).count();

    // --- 8. DEVICE -> HOST TRANSFER ---
    t1 = chrono::high_resolution_clock::now();
    uint8_t* resultData = new uint8_t[numPixels];
    cudaMemcpy(resultData, d_blurred, numPixels, cudaMemcpyDeviceToHost);
    t2 = chrono::high_resolution_clock::now();
    t_d2h = chrono::duration<double>(t2 - t1).count();

    // Cleanup device memory
    cudaFree(d_rgb); cudaFree(d_gray); cudaFree(d_blurred);
    cudaFree(d_mag); cudaFree(d_dir); cudaFree(d_edges);
    cudaFree(d_kernel); cudaFree(d_changed);
    
    // --- PRINT BREAKDOWN ---
    // Use CRITICAL to avoid mixing output from different OpenMP threads
    #pragma omp critical
    {
        cout << "\n[Thread " << omp_get_thread_num() << "] CUDA Breakdown:\n";
        cout << "----------------------------------------\n";
        cout << "  Alloc GPU Mem:   " << fixed << t_alloc << " s\n";
        cout << "  Memcpy H->D:     " << t_h2d << " s\n";
        cout << "  Grayscale:       " << t_gray << " s\n";
        cout << "  Gaussian Blur:   " << t_blur << " s\n";
        cout << "  Sobel:           " << t_sobel << " s\n";
        cout << "  NMS:             " << t_nms << " s\n";
        cout << "  Hysteresis:      " << t_hyst << " s (" << iterations << " iters)\n";
        cout << "  Memcpy D->H:     " << t_d2h << " s\n";
        
        double t_kernels_only = t_gray + t_blur + t_sobel + t_nms + t_hyst;
        cout << "  > Total Kernel:  " << t_kernels_only << " s\n";
        cout << "----------------------------------------\n";
    }
    
    return resultData;
}

// Function to print usage instructions
void printUsage(const char* programName) {
    cout << "Usage: " << programName << " <input_folder> [output_folder] [low_threshold]"
                                            " [high_threshold] [kernel_size] [sigma] [block_size] [threads]\n";
    cout << "\nArguments:\n";
    cout << "  input_folder      - Input folder containing images\n";
    cout << "  output_folder     - (Optional) Output PNG file (default: output_canny)\n";
    cout << "  low_threshold     - (Optional) Lower threshold (default: 15)\n";
    cout << "  high_threshold    - (Optional) Upper threshold (default: 40)\n";
    cout << "  kernel_size       - (Optional) Gaussian kernel size (default: 9)\n";
    cout << "  sigma             - (Optional) Gaussian sigma (default: 1.5)\n";
    cout << "  block_size        - (Optional) CUDA block size (default: 16)\n";
    cout << "  threads           - (Optional) Number of OpenMP threads (default: max available)\n";
}

// Function to check if a file has a specific extension
bool hasExtension(const std::string& filename, const std::string& ext) {
    if (filename.length() >= ext.length()) {
        return (0 == filename.compare(filename.length() - ext.length(), ext.length(), ext));
    }
    return false;
}

// Function to extract all images from a folder
vector<string> getAllImages(char* folderPath) {
    vector<string> files;
    struct dirent *entry;
    DIR *dp;

    dp = opendir(folderPath);
    if (dp == NULL) {
        std::cerr << "Directory couldn't be opened: " << folderPath << std::endl;
        return files;
    }

    while ((entry = readdir(dp))) {
        std::string filename = entry->d_name;

        // Ignore . and .. in the folder
        if (filename == "." || filename == "..") continue;

        // Get file extension
        if (hasExtension(filename, ".jpg") || hasExtension(filename, ".png") || 
            hasExtension(filename, ".jpeg") || hasExtension(filename, ".JPG")) {
            
            // Construct full path
            std::string fullPath = std::string(folderPath) + "/" + filename;
            files.push_back(fullPath);
        }
    }

    closedir(dp);
    return files;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return -1;
    }
    
    // Parse arguments
    string inputPath = argv[1];
    string outputPath = (argc >= 3) ? argv[2] : "output_canny";

    int lowThreshold = 15;
    int highThreshold = 40;
    int kernelSize = 9;
    int blockSizeValue = 16;
    float sigma = 1.5f;

    // Reading arguments
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
    
    // Validation for arguments
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

    // Create output folder
    struct stat st = {0};
    if (stat(outputPath.c_str(), &st) == -1) {
        mkdir(outputPath.c_str(), 0777);
    }

    // Obtain all photos
    vector<string> imageFiles = getAllImages((char*)inputPath.c_str());
    if(imageFiles.empty()) {
        cerr << "Error: No images found in the specified input folder.\n";
        return -1;
    }
    #pragma omp parallel for schedule(dynamic)
    for(int i = 0; i < imageFiles.size(); i++) {
        //Manual path extraction
        string filePath = imageFiles[i];
        
        string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);
        fileName = fileName.substr(0, fileName.find_last_of('.'));

        string outputFilePath = outputPath + "/" + fileName + "_canny.png";
        
        // Load image
        int width, height, channels;
        uint8_t* h_rgb = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

        if (!h_rgb) {
            #pragma omp critical
            {
                cerr << "Error: Could not load image '" << filePath << "'\n";
                cerr << "   Reason: " << stbi_failure_reason() << "\n";
            }
            continue;
        }
        #pragma omp critical
        {
            cout << "Thread " << omp_get_thread_num() << " processing: " << fileName << " (" << width << "x" << height << ")\n";
        }

        auto start = chrono::high_resolution_clock::now();

        // Apply Canny Edge Detection using CUDA
        uint8_t* edges = cannyEdgeDetection(h_rgb, width, height, channels, lowThreshold, highThreshold,
                                kernelSize, sigma, blockSizeValue);

        auto end = chrono::high_resolution_clock::now();
        auto elapsed = chrono::duration<double>(end - start).count();

        if(!stbi_write_png(outputFilePath.c_str(), width, height, 1, edges, width)) {
            #pragma omp critical
            cerr << "Error: Could not save image to '" << outputFilePath << "'\n";
        } else {
            // Edge pixels percentage
            int totalPixels = width * height;
            int edgePixels = 0;
            for (int k = 0; k < totalPixels; k++) {
                if (edges[k] == 255) edgePixels++;
            }
            double edgePercentage = (double)edgePixels / totalPixels * 100.0;

            #pragma omp critical
            {
                cout << "  [DONE] " << fileName << " in " << elapsed << " s. Edges: " << fixed << edgePercentage << "%\n";
            }
        }

        // Cleanup
        stbi_image_free(h_rgb);
        delete[] edges;
    }

    return 0;
}