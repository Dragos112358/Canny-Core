#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <iomanip> // Necesar pentru setprecision si fixed

using namespace std;

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


// Sobel gradient function to compute magnitude and direction
void sobelGradient(const Image& img, vector<float>& magnitude, vector<float>& direction) {
    magnitude.resize(img.width * img.height);
    direction.resize(img.width * img.height);
    
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
    
    // Compute gradients
    for (int y = 1; y < img.height - 1; y++) {
        for (int x = 1; x < img.width - 1; x++) {
            float gx = 0.0f, gy = 0.0f;
            
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    uint8_t pixel = img.at(x + kx, y + ky);
                    gx += pixel * Gx[ky + 1][kx + 1];
                    gy += pixel * Gy[ky + 1][kx + 1];
                }
            }
            
            int idx = y * img.width + x;
            magnitude[idx] = sqrtf(gx * gx + gy * gy);
            direction[idx] = atan2f(gy, gx);
        }
    }
}

// Non-Maximum Suppression
Image nonMaxSuppression(int width, int height, const vector<float>& magnitude, const vector<float>& direction) {
    Image suppressed = Image(width, height, 1);
    
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float mag = magnitude[idx];
            float angle = direction[idx] * 180.0f / M_PI;
            
            // Normalize angle to [0, 180)
            if (angle < 0)
                angle += 180.0f;
            
            float neighbor1 = 0, neighbor2 = 0;
            
            // Check neighbors in the gradient direction
            if ((angle >= 0 && angle < 22.5) || (angle >= 157.5 && angle < 180)) {
                // horizontal
                neighbor1 = magnitude[idx - 1];
                neighbor2 = magnitude[idx + 1];
            } else if (angle >= 22.5 && angle < 67.5) {
                // diagonal
                neighbor1 = magnitude[idx - width + 1];
                neighbor2 = magnitude[idx + width - 1];
            } else if (angle >= 67.5 && angle < 112.5) {
                // vertical
                neighbor1 = magnitude[idx - width];
                neighbor2 = magnitude[idx + width];
            } else if (angle >= 112.5 && angle < 157.5) {
                // diagonal
                neighbor1 = magnitude[idx - width - 1];
                neighbor2 = magnitude[idx + width + 1];
            }
            
            // Keep only local maxima
            if (mag >= neighbor1 && mag >= neighbor2) {
                suppressed.at(x, y) = static_cast<uint8_t>(min(mag, 255.0f));
            } else {
                suppressed.at(x, y) = 0;
            }
        }
    }
    
    return suppressed;
}


// Recursive tracking (DFS) for weak edges connected to strong edges
void trackEdge(int x, int y, int width, int height, Image& result, vector<bool>& visited) {
    // Check if position is invalid
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;

    int idx = y * width + x;

    if (visited[idx])
        return;

    if (result.at(x, y) != 128)
        return;
    
    visited[idx] = true;
    // Promote weak edge to strong edge
    result.at(x, y) = 255;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {

            if (dx == 0 && dy == 0)
                continue;

            trackEdge(x + dx, y + dy, width, height, result, visited);
        }
    }
}


// Double Threshold + Edge Tracking by Hysteresis
Image hysteresisThreshold(const Image& img, int lowThreshold, int highThreshold) {
    Image result = Image(img.width, img.height, 1);
    vector<bool> visited(img.width * img.height, false);
    
    // Mark strong edges
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {

            uint8_t val = img.at(x, y);
            if (val >= highThreshold) {
                result.at(x, y) = 255;
            } else if (val >= lowThreshold) {
                // weak edge (candidate)
                result.at(x, y) = 128;
            } else {
                result.at(x, y) = 0;
            }
        }
    }

    // Track edges
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {

            if (result.at(x, y) == 255 && !visited[y * img.width + x]) {

                visited[y * img.width + x] = true;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        trackEdge(x + dx, y + dy, img.width, img.height, result, visited);
                    }
                }
            }
        }
    }


    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            if (result.at(x, y) == 128) {
                result.at(x, y) = 0;
            }
        }
    }
    
    return result;
}

// Pipeline function to perform Canny Edge Detection with Timings
Image cannyEdgeDetection(const Image& input, int lowThreshold, int highThreshold,
                            int kernelSize, float sigma) {
    
    // 1. Start Total Timer
    auto start_total = std::chrono::high_resolution_clock::now();

    // --- Grayscale ---
    auto t1 = std::chrono::high_resolution_clock::now();
    Image gray = Image::toGrayscale(input);
    auto t2 = std::chrono::high_resolution_clock::now();
    
    // --- Gaussian Blur ---
    Image blurred = Image::gaussianBlur(gray, kernelSize, sigma);
    auto t3 = std::chrono::high_resolution_clock::now();
    
    // --- Sobel ---
    vector<float> magnitude, direction;
    sobelGradient(blurred, magnitude, direction);
    auto t4 = std::chrono::high_resolution_clock::now();
    
    // --- Non-Max Suppression ---
    Image suppressed = nonMaxSuppression(blurred.width, blurred.height, magnitude, direction);
    auto t5 = std::chrono::high_resolution_clock::now();
    
    // --- Hysteresis ---
    Image edges = hysteresisThreshold(suppressed, lowThreshold, highThreshold);
    auto t6 = std::chrono::high_resolution_clock::now();

    // Calculate Durations
    double time_gray = std::chrono::duration<double>(t2 - t1).count();
    double time_blur = std::chrono::duration<double>(t3 - t2).count();
    double time_sobel = std::chrono::duration<double>(t4 - t3).count();
    double time_nms = std::chrono::duration<double>(t5 - t4).count();
    double time_hyst = std::chrono::duration<double>(t6 - t5).count();
    double time_total = std::chrono::duration<double>(t6 - start_total).count();

    // Print Formatted Output 
    cout << fixed << setprecision(4);
    cout << "Total Pipeline Time: " << time_total << " s\n";
    cout << "----------------------------------------\n";
    cout << "Breakdown (Rank 0 timings):\n";
    cout << "  Grayscale:      " << time_gray << " s\n";
    cout << "  Gaussian blur:  " << time_blur << " s\n";
    cout << "  Sobel:          " << time_sobel << " s\n";
    cout << "  Non-Max Suppr:  " << time_nms << " s\n";
    cout << "  Hysteresis:     " << time_hyst << " s\n";
    cout << "\n"; // Linie nouă pentru separare

    return edges;
}

// Function to print usage instructions
void printUsage(const char* programName) {
    cout << "Usage: " << programName << " <input_image> [output_image.png] [low_threshold]"
                                          " [high_threshold] [kernel_size] [sigma]\n";
    cout << "\nArguments:\n";
    cout << "  input_image       - Path to input image (jpg, png, bmp, tga, etc.)\n";
    cout << "  output_image.png  - (Optional) Output PNG file (default: output_canny.png)\n";
    cout << "  low_threshold     - (Optional) Lower threshold (default: 15)\n";
    cout << "  high_threshold    - (Optional) Upper threshold (default: 40)\n";
    cout << "  kernel_size       - (Optional) Gaussian kernel size (default: 9)\n";
    cout << "  sigma             - (Optional) Gaussian sigma (default: 1.5)\n";
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
    float sigma = 1.5f;

    for(int i = 3; i < argc; i++) {
        switch(i) {
            case 3: lowThreshold = stoi(argv[i]); break;
            case 4: highThreshold = stoi(argv[i]); break;
            case 5: kernelSize = stoi(argv[i]); break;
            case 6: sigma = stof(argv[i]); break;
            default: break;
        }
    }
    
    if (lowThreshold < 0 || highThreshold < 0 || lowThreshold >= highThreshold) {
        cerr << "Error: Invalid thresholds. Ensure 0 <= low < high\n";
        return -1;
    }

    // Load image
    cout << "Loading image...\n";
    int width, height, channels;
    uint8_t* imageData = stbi_load(inputPath.c_str(), &width, &height, &channels, 0);

    if (!imageData) {
        cerr << "Error: Could not load image '" << inputPath << "'\n";
        cerr << "   Reason: " << stbi_failure_reason() << "\n";
        return -1;
    }
    cout << "Image loaded: " << width << "x" << height << " pixels, " << channels << " channels\n\n";

    // Copy to Image structure
    Image input = Image(width, height, channels);
    copy(imageData, imageData + width * height * channels, input.data.begin());
    stbi_image_free(imageData);


    // Apply Canny (Timings are now printed inside this function)
    Image edges = cannyEdgeDetection(input, lowThreshold, highThreshold, kernelSize, sigma);

    cout << "Saving result to '" << outputPath << "'...\n";

    if (!stbi_write_png(outputPath.c_str(), edges.width, edges.height, 1, 
                        edges.data.data(), edges.width)) {
        cerr << "Error: Could not save image to '" << outputPath << "'\n";
        return -1;
    }
    
    cout << "Success! Edge detection completed.\n";
    cout << "   Output saved: " << outputPath << "\n";
    
    // Statistics
    int totalPixels = edges.width * edges.height;
    int edgePixels = 0;
    for (uint8_t pixel : edges.data) {

        if (pixel > 0)
            edgePixels++;
    }

    double edgePercentage = (edgePixels * 100.0) / totalPixels;
    
    cout << "\nStatistics:\n";
    cout << "   Total pixels: " << totalPixels << "\n";
    cout << "   Edge pixels:  " << edgePixels << " (" << fixed << setprecision(4) << edgePercentage << "%)\n";
    
    return 0;
}