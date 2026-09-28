#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <chrono>
#include <omp.h>
#include <iomanip>

using namespace std;

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

enum Stage {
    STAGE_GRAY = 0,
    STAGE_BLUR,
    STAGE_SOBEL,
    STAGE_NMS,
    STAGE_HYST,
    STAGE_COUNT
};

// Sobel gradient
void sobelGradient(const Image& img, vector<float>& magnitude, vector<float>& direction) {
    int w = img.width;
    int h = img.height;
    magnitude.resize(w * h);
    direction.resize(w * h);
    
    const int Gx[3][3] = { {-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1} };
    const int Gy[3][3] = { {-1, -2, -1}, {0, 0, 0}, {1, 2, 1} };
    
    // gathering gradients in parallel
    #pragma omp parallel for schedule(static)
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            float gx = 0.0f, gy = 0.0f;
            
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    uint8_t pixel = img.at(x + kx, y + ky);
                    gx += pixel * Gx[ky + 1][kx + 1];
                    gy += pixel * Gy[ky + 1][kx + 1];
                }
            }
            
            int idx = y * w + x;
            magnitude[idx] = sqrtf(gx * gx + gy * gy);
            direction[idx] = atan2f(gy, gx);
        }
    }
}

// Non-Maximum Suppression
Image nonMaxSuppression(int width, int height, const vector<float>& magnitude, const vector<float>& direction) {
    Image suppressed(width, height, 1);
    
    #pragma omp parallel for schedule(static)
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float mag = magnitude[idx];
            float angle = direction[idx] * 180.0f / M_PI;
            
            // normalize angle to [0, 180)
            if (angle < 0)
                angle += 180.0f;
            
            float neighbor1 = 0, neighbor2 = 0;
            
            // check neighbors in the gradient direction
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
            
            // keep only local maxim
            if (mag >= neighbor1 && mag >= neighbor2) {
                suppressed.at(x, y) = static_cast<uint8_t>(min(mag, 255.0f));
            } else {
                suppressed.at(x, y) = 0;
            }
        }
    }
    
    return suppressed;
}

// recursive tracking (DFS)
void trackEdge(int x, int y, int width, int height, Image& result, vector<uint8_t>& visited) {
    // check if position is invalid or already visited
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;

    int idx = y * width + x;
    if (visited[idx])
        return;

    // we only track from weak edges
    if (result.at(x, y) != 128)
        return;
    
    visited[idx] = 1;

    // promote weak edge to strong edge
    result.at(x, y) = 255;
    
    // recurse to all 8 neighbors
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            trackEdge(x + dx, y + dy, width, height, result, visited);
        }
    }
}

// Double Threshold + Hysteresis
Image hysteresisThreshold(const Image& img, int lowThreshold, int highThreshold) {
    Image result(img.width, img.height, 1);
    int w = img.width;
    int h = img.height;
    vector<uint8_t> visited(w * h, 0);
    

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t val = img.at(x, y);
            if (val >= highThreshold) {
                // mark as being strong
                result.at(x, y) = 255;
            } else if (val >= lowThreshold) {
                // mark as being weak
                result.at(x, y) = 128;
            } else {
                // mark as being off
                result.at(x, y) = 0;
            }
        }
    }

    // sequential edge tracking
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // if we find a strong pixel that we haven't visited, start tracking from it
            if (result.at(x, y) == 255 && !visited[y * w + x]) {
                visited[y * w + x] = 1;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        trackEdge(x + dx, y + dy, img.width, img.height, result, visited);
                    }
                }
            }
        }
    }

    // set remaining weak edges to off
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < w * h; i++) {
        if (result.data[i] == 128) {
            result.data[i] = 0;
        }
    }
    
    return result;
}

// pipeline function to perform Canny Edge Detection
Image cannyEdgeDetection(const Image& input, int lowThreshold, int highThreshold,
                         int kernelSize, float sigma, vector<double>& timings) {
    
    double t1, t2;

    // 1. Grayscale
    // cout << "Converting to grayscale...\n";
    t1 = omp_get_wtime();
    Image gray = Image::toGrayscale(input);
    t2 = omp_get_wtime();
    timings[STAGE_GRAY] = t2 - t1;
    
    // 2. Gaussian Blur
    // cout << "Applying Gaussian blur...\n";
    t1 = omp_get_wtime();
    Image blurred = Image::gaussianBlur(gray, kernelSize, sigma);
    t2 = omp_get_wtime();
    timings[STAGE_BLUR] = t2 - t1;
    
    // 3. Sobel
    // cout << "Computing gradients (Sobel)...\n";
    t1 = omp_get_wtime();
    vector<float> magnitude, direction;
    sobelGradient(blurred, magnitude, direction);
    t2 = omp_get_wtime();
    timings[STAGE_SOBEL] = t2 - t1;
    
    // 4. Non-Max Suppression
    // cout << "Non-maximum suppression...\n";
    t1 = omp_get_wtime();
    Image suppressed = nonMaxSuppression(blurred.width, blurred.height, magnitude, direction);
    t2 = omp_get_wtime();
    timings[STAGE_NMS] = t2 - t1;
    
    // 5. Hysteresis
    // cout << "Double threshold + hysteresis...\n";
    t1 = omp_get_wtime();
    Image edges = hysteresisThreshold(suppressed, lowThreshold, highThreshold);
    t2 = omp_get_wtime();
    timings[STAGE_HYST] = t2 - t1;
    
    return edges;
}

// function to print usage instructions
void printUsage(const char* programName) {
    cout << "Usage: " << programName << " <input_image> [output_image.png] [low_threshold]"
                                        " [high_threshold] [kernel_size] [sigma] [num_threads]\n";
    cout << "\nArguments:\n";
    cout << "  input_image       - Path to input image (jpg, png, bmp, etc.)\n";
    cout << "  output_image.png  - Output file (default: output_canny.png)\n";
    cout << "  low_threshold     - Default: 15\n";
    cout << "  high_threshold    - Default: 40\n";
    cout << "  kernel_size       - Default: 9\n";
    cout << "  sigma             - Default: 1.5\n";
    cout << "  num_threads       - Default: Maximum available\n";
}


int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return -1;
    }
    vector<double> timings(STAGE_COUNT, 0.0);
    // parse arguments
    string inputPath = argv[1];
    string outputPath = (argc >= 3) ? argv[2] : "output_canny.png";

    int lowThreshold = 15;
    int highThreshold = 40;
    int kernelSize = 9;
    float sigma = 1.5f;
    int numThreads = omp_get_max_threads();

    for(int i = 3; i < argc; i++) {
        switch(i) {
            case 3: lowThreshold = stoi(argv[i]); break;
            case 4: highThreshold = stoi(argv[i]); break;
            case 5: kernelSize = stoi(argv[i]); break;
            case 6: sigma = stof(argv[i]); break;
            case 7: numThreads = stoi(argv[i]); break;
            default: break;
        }
    }

    omp_set_num_threads(numThreads);
    
    if (lowThreshold < 0 || highThreshold < 0 || lowThreshold >= highThreshold) {
        cerr << "Error: Invalid thresholds. Ensure 0 <= low < high\n";
        return -1;
    }

    // load image
    cout << "Loading image...\n";
    int width, height, channels;
    uint8_t* imageData = stbi_load(inputPath.c_str(), &width, &height, &channels, 0);

    if (!imageData) {
        cerr << "Error: Could not load image '" << inputPath << "'\n";
        cerr << "   Reason: " << stbi_failure_reason() << "\n";
        return -1;
    }
    cout << "Image loaded: " << width << "x" << height << " pixels, " << channels << " channels\n\n";

    // copy to Image structure
    Image input(width, height, channels);

    // parallel copy of image data
    #pragma omp parallel for
    for(int i = 0; i < width * height * channels; i++) {
        input.data[i] = imageData[i];
    }
    stbi_image_free(imageData);

    // apply Canny
    cout << "Running Canny Edge Detection with " << omp_get_max_threads() << " threads...\n";

    auto start = std::chrono::high_resolution_clock::now();

    Image edges = cannyEdgeDetection(input, lowThreshold, highThreshold, kernelSize, sigma, timings);

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<double>(end - start).count();

    cout << "========================================\n";
    cout << "OpenMP Canny Edge Detection Completed\n";
    cout << "Image Size: " << width << "x" << height << "\n";
    cout << "Threads:    " << numThreads << "\n";
    cout << "Total Time: " << fixed << setprecision(4) << elapsed << " s\n";
    cout << "----------------------------------------\n";
    cout << "Breakdown:\n";
    cout << "  Grayscale:      " << timings[STAGE_GRAY] << " s\n";
    cout << "  Gaussian blur:  " << timings[STAGE_BLUR] << " s\n";
    cout << "  Sobel:          " << timings[STAGE_SOBEL] << " s\n";
    cout << "  NMS:            " << timings[STAGE_NMS] << " s\n";
    cout << "  Hysteresis:     " << timings[STAGE_HYST] << " s\n";
    cout << "========================================\n";

    std::cout << "Elapsed time: " << elapsed << " seconds\n";

    cout << "\nSaving result to '" << outputPath << "'...\n";

    if (!stbi_write_png(outputPath.c_str(), edges.width, edges.height, 1, 
                        edges.data.data(), edges.width)) {
        cerr << "Error: Could not save image to '" << outputPath << "'\n";
        return -1;
    }
    
    cout << "Success!\n";
    
    int totalPixels = edges.width * edges.height;
    int edgePixels = 0;
    
    // parallel reduction for stats
    #pragma omp parallel for reduction(+:edgePixels)
    for (int i=0; i < edges.data.size(); i++) {
        if (edges.data[i] > 0) edgePixels++;
    }

    double edgePercentage = (edgePixels * 100.0) / totalPixels;
    
    cout << "\nStatistics:\n";
    cout << "   Total pixels: " << totalPixels << "\n";
    cout << "   Edge pixels:  " << edgePixels << " (" << fixed << edgePercentage << "%)\n";
    
    return 0;
}

