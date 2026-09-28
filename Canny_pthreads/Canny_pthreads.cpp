#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstdint>
#include <pthread.h>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace std;

#include "Image.h"

struct NMSArgs {
    int id, totalThreads;
    int width, height;
    const vector<float>* magnitude;
    const vector<float>* direction;
    Image* output;
};

struct ThresholdArgs {
    int id, totalThreads;
    int low, high;
    const Image* input;
    Image* output;
};


void* threadSobel(void* arg) {
    SobelArgs* args = (SobelArgs*)arg;
    int width  = args->img->width;
    int height = args->img->height;

    // Numărul de rânduri de procesat este interiorul imaginii
    int interiorRows = height - 2;  // exclude rândurile 0 și h-1
    int hStart = args->id * interiorRows / args->totalThreads + 1;
    int hEnd   = (args->id + 1) * interiorRows / args->totalThreads + 1; // exclusiv
    if(hEnd == height)
    {
        hEnd--;
    }
    //cout << hStart <<" "<<hEnd<<"\n";
    const uint8_t* img = args->img->data.data();
    float* mag = args->magnitude->data();
    float* dir = args->direction->data();

    // Sobel kernels
    static const int Gx[3][3] = { {-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1} };
    static const int Gy[3][3] = { {-1, -2, -1}, {0, 0, 0}, {1, 2, 1} };

    for (int y = hStart; y < hEnd; y++) {
        for (int x = 1; x < width - 1; x++) {
            float gx = 0.0f, gy = 0.0f;

            // exact ca OpenMP
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    uint8_t pixel = args->img->at(x + kx, y + ky);
                    gx += pixel * Gx[ky + 1][kx + 1];
                    gy += pixel * Gy[ky + 1][kx + 1];
                }
            }

            int idx = y * width + x;
            mag[idx] = sqrtf(gx * gx + gy * gy);
            dir[idx] = atan2f(gy, gx);
        }
    }

    return nullptr;
}

void sobelGradientPthreads(const Image& img, vector<float>& magnitude, 
                           vector<float>& direction, int numThreads) {
    magnitude.resize(img.width * img.height, 0.0f);
    direction.resize(img.width * img.height, 0.0f);

    pthread_t threads[numThreads];
    SobelArgs args[numThreads];

    for (int i = 0; i < numThreads; i++) {
        args[i] = {i, numThreads, &img, &magnitude, &direction};
        pthread_create(&threads[i], nullptr, threadSobel, &args[i]);
    }

    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }
}


void* threadNMS(void* arg) {
    NMSArgs* args = (NMSArgs*)arg;
    int width  = args->width;
    int height = args->height;

    int hStart = args->id * height / args->totalThreads;
    int hEnd   = (args->id + 1) * height / args->totalThreads;

    const float* mag = args->magnitude->data();
    const float* dir = args->direction->data();
    uint8_t* out = args->output->data.data();

    // Process only lines 1...height-2
    if (hStart == 0) hStart = 1;
    if (hEnd == height-1) hEnd = height;
    //cout<<hStart<<" "<<hEnd<<"\n";
    static const float PI_180 = 180.0f / M_PI;

    for (int y = hStart; y < hEnd; y++) {
        const float* magRow = mag + y * width;
        const float* dirRow = dir + y * width;
        uint8_t* outRow = out + y * width;

        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float m = mag[idx];
            float angle = dir[idx] * PI_180;
            if (angle < 0) angle += 180.0f;

            float neighbor1 = 0.0f, neighbor2 = 0.0f;

            // Check neighbors in the gradient direction
            if ((angle >= 0 && angle < 22.5f) || (angle >= 157.5f && angle < 180.0f)) {
                neighbor1 = mag[idx - 1];
                neighbor2 = mag[idx + 1];
            } else if (angle >= 22.5f && angle < 67.5f) {
                neighbor1 = mag[idx - width + 1];
                neighbor2 = mag[idx + width - 1];
            } else if (angle >= 67.5f && angle < 112.5f) {
                neighbor1 = mag[idx - width];
                neighbor2 = mag[idx + width];
            } else if (angle >= 112.5f && angle < 157.5f) {
                neighbor1 = mag[idx - width - 1];
                neighbor2 = mag[idx + width + 1];
            }

            // Keep only local maxima
            outRow[x] = (m >= neighbor1 && m >= neighbor2) ? static_cast<uint8_t>(std::min(m, 255.0f)) : 0;
        }
    }

    return nullptr;
}


Image nonMaxSuppressionPthreads(int width, int height,
                                const vector<float>& magnitude,
                                const vector<float>& direction,
                                int numThreads) {
    Image suppressed(width, height, 1);
    pthread_t threads[numThreads];
    NMSArgs args[numThreads];
    
    for (int i = 0; i < numThreads; i++) {
        args[i] = {i, numThreads, width, height, &magnitude, &direction, &suppressed};
        pthread_create(&threads[i], nullptr, threadNMS, &args[i]);
    }
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    return suppressed;
}


void* threadThreshold(void* arg) {
    ThresholdArgs* args = (ThresholdArgs*)arg;
    int width = args->input->width;
    int height = args->input->height;
    
    int hStart = args->id * height / args->totalThreads;
    int hEnd = (args->id + 1) * height / args->totalThreads;

    const uint8_t* inData = args->input->data.data();
    uint8_t* outData = args->output->data.data();
    int high = args->high;
    int low = args->low;

    // Process in chunks for better cache locality
    for (int y = hStart; y < hEnd; y++) {
        const uint8_t* inRow = inData + y * width;
        uint8_t* outRow = outData + y * width;
        
        for (int x = 0; x < width; x++) {
            uint8_t val = inRow[x];
            
            // Branchless threshold (faster on modern CPUs)
            outRow[x] = (val >= high) ? 255 : ((val >= low) ? 128 : 0);
        }
    }
    pthread_exit(nullptr);
}

inline void trackEdge(int x, int y, int width, int height, uint8_t* data, uint8_t* visited) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;

    int idx = y * width + x;
    if (visited[idx] || data[idx] != 128) return;

    visited[idx] = 1;
    data[idx] = 255;

    // Track 8-connected neighbors - SAME ORDER AS OPENMP
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            trackEdge(x + dx, y + dy, width, height, data, visited);
        }
    }
}

void* threadCleanup(void* arg) {
    ThresholdArgs* args = (ThresholdArgs*)arg;
    int width = args->output->width;
    int height = args->output->height;
    
    int hStart = args->id * height / args->totalThreads;
    int hEnd = (args->id + 1) * height / args->totalThreads;
    
    uint8_t* data = args->output->data.data();
    
    // Process rows in chunks for better cache performance
    for (int y = hStart; y < hEnd; y++) {
        uint8_t* row = data + y * width;
        for (int x = 0; x < width; x++) {
            // Branchless cleanup
            row[x] = (row[x] == 128) ? 0 : row[x];
        }
    }
    pthread_exit(nullptr);
}

Image hysteresisThresholdPthreads(const Image& img, int lowThreshold, 
                                  int highThreshold, int numThreads) {
    Image result(img.width, img.height, 1);
    vector<uint8_t> visited(img.width * img.height, 0);
    
    pthread_t threads[numThreads];
    ThresholdArgs args[numThreads];
    
    // Step 1: Parallel classification into strong/weak/non-edges
    for (int i = 0; i < numThreads; i++) {
        args[i] = {i, numThreads, lowThreshold, highThreshold, &img, &result};
        pthread_create(&threads[i], nullptr, threadThreshold, &args[i]);
    }
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }

    // Step 2: Sequential edge tracking (recursive DFS)
    uint8_t* resultData = result.data.data();
    uint8_t* visitedData = visited.data();
    
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            int idx = y * img.width + x;
            if (resultData[idx] == 255 && !visitedData[idx]) {
                visitedData[idx] = 1;
                // Track all connected weak edges - SAME ORDER AS OPENMP
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        trackEdge(x + dx, y + dy, img.width, img.height, resultData, visitedData);
                    }
                }
            }
        }
    }

    // Step 3: Parallel cleanup of remaining weak edges
    for (int i = 0; i < numThreads; i++) {
        args[i] = {i, numThreads, 0, 0, nullptr, &result};
        pthread_create(&threads[i], nullptr, threadCleanup, &args[i]);
    }
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    return result;
}


Image cannyEdgeDetection(const Image& input, int lowThreshold, int highThreshold, 
                         int kernelSize, float sigma, int numThreads) 
{
    using namespace chrono;

    cout << "Converting to grayscale...\n";
    auto t1 = high_resolution_clock::now();
    Image gray = toGrayscalePthreads(input, numThreads);
    auto t2 = high_resolution_clock::now();
    double tGray = duration<double>(t2 - t1).count();
    cout << "   Grayscale time: " << tGray << " s\n";
    cout << "Applying Gaussian blur (kernel=" << kernelSize << ", sigma=" << sigma << ")...\n";
    t1 = high_resolution_clock::now();
    Image blurred = gaussianBlurPthreads(gray, kernelSize, sigma, numThreads);

    t2 = high_resolution_clock::now();
    double tBlur = duration<double>(t2 - t1).count();
    cout << "   Gaussian blur time: " << tBlur << " s\n";

    cout << "Computing gradients (Sobel)...\n";
    t1 = high_resolution_clock::now();
    vector<float> magnitude, direction;
    sobelGradientPthreads(blurred, magnitude, direction, numThreads);
    t2 = high_resolution_clock::now();
    
    double tSobel = duration<double>(t2 - t1).count();
    cout << "   Sobel time: " << tSobel << " s\n";

    cout << "Non-maximum suppression...\n";
    t1 = high_resolution_clock::now();
    Image suppressed = nonMaxSuppressionPthreads(
        blurred.width, blurred.height, magnitude, direction, numThreads
    );
    t2 = high_resolution_clock::now();
    double tNMS = duration<double>(t2 - t1).count();
    cout << "   NMS time: " << tNMS << " s\n";

    cout << "Double threshold + hysteresis...\n";
    t1 = high_resolution_clock::now();
    Image edges = hysteresisThresholdPthreads(
        suppressed, lowThreshold, highThreshold, numThreads
    );
    t2 = high_resolution_clock::now();
    double tHyst = duration<double>(t2 - t1).count();
    cout << "   Hysteresis time: " << tHyst << " s\n\n";

    return edges;
}

// ============================================================================
// MAIN
// ============================================================================

void printUsage(const char* programName) {
    cout << "Usage: " << programName << " <input_image> [output_image.png] "
         << "[low_threshold] [high_threshold] [kernel_size] [sigma] [num_threads]\n";
    cout << "\nArguments:\n";
    cout << "  input_image       - Path to input image (jpg, png, bmp, etc.)\n";
    cout << "  output_image.png  - Output file (default: output_pthreads.png)\n";
    cout << "  low_threshold     - Default: 15\n";
    cout << "  high_threshold    - Default: 45\n";
    cout << "  kernel_size       - Gaussian kernel size (default: 9, must be odd)\n";
    cout << "  sigma             - Gaussian sigma (default: 1.5)\n";
    cout << "  num_threads       - Default: 4\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return -1;
    }

    string inputPath = argv[1];
    string outputPath = (argc >= 3) ? argv[2] : "output_pthreads.png";
    int lowThreshold = (argc >= 4) ? stoi(argv[3]) : 15;
    int highThreshold = (argc >= 5) ? stoi(argv[4]) : 45;
    int kernelSize = (argc >= 6) ? stoi(argv[5]) : 9;
    float sigma = (argc >= 7) ? stof(argv[6]) : 1.5f;
    int numThreads = (argc >= 8) ? stoi(argv[7]) : 4;

    if (lowThreshold < 0 || highThreshold < 0 || lowThreshold >= highThreshold) {
        cerr << "Error: Invalid thresholds. Ensure 0 <= low < high\n";
        return -1;
    }

    if (kernelSize < 3) {
        cerr << "Error: Kernel size must be at least 3\n";
        return -1;
    }

    // Load image
    cout << "Loading image...\n";
    int width, height, channels;
    uint8_t* data = stbi_load(inputPath.c_str(), &width, &height, &channels, 0);
    
    if (!data) {
        cerr << "Error: Could not load image '" << inputPath << "'\n";
        cerr << "   Reason: " << stbi_failure_reason() << "\n";
        return -1;
    }
    
    cout << "Image loaded: " << width << "x" << height << " pixels, " 
         << channels << " channels\n\n";

    Image input(width, height, channels);
    copy(data, data + width * height * channels, input.data.begin());
    stbi_image_free(data);

    // Apply Canny
    cout << "Running Canny Edge Detection with " << numThreads << " threads...\n";
    auto start = chrono::high_resolution_clock::now();
    
    Image edges = cannyEdgeDetection(input, lowThreshold, highThreshold, 
                                     kernelSize, sigma, numThreads);
    
    auto end = chrono::high_resolution_clock::now();
    auto elapsed = chrono::duration<double>(end - start).count();
    
    cout << "\nElapsed time: " << elapsed << " seconds\n";

    // Save result
    cout << "Saving result to '" << outputPath << "'...\n";
    if (!stbi_write_png(outputPath.c_str(), edges.width, edges.height, 1, 
                        edges.data.data(), edges.width)) {
        cerr << "Error: Could not save image to '" << outputPath << "'\n";
        return -1;
    }

    // Statistics
    int edgePixels = 0;
    for (int i = 0; i < edges.width * edges.height; i++) {
        if (edges.data[i] == 255) edgePixels++;
    }

    double percent = 100.0 * edgePixels / (edges.width * edges.height);
    
    cout << "\nStatistics:\n";
    cout << "   Total pixels: " << edges.width * edges.height << "\n";
    cout << "   Edge pixels:  " << edgePixels << " (" << percent << "%)\n";
    cout << "\nSuccess!\n";
    
    return 0;
}