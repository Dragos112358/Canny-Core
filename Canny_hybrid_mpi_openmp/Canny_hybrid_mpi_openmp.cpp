#include <mpi.h>
#include <omp.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip> 

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace std;

// enum for the stage
enum Stage {
    STAGE_GRAY = 0,
    STAGE_BLUR,
    STAGE_SOBEL,
    STAGE_NMS,
    STAGE_HYST,
    STAGE_COUNT
};

// Sobel Gradient (OpenMP)
void sobelGradient(const Image& img, vector<float>& magnitude, vector<float>& direction) {
    int w = img.width;
    int h = img.height;
    magnitude.resize(w * h);
    direction.resize(w * h);
    
    const int Gx[3][3] = { {-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1} };
    const int Gy[3][3] = { {-1, -2, -1}, {0, 0, 0}, {1, 2, 1} };
    
    const uint8_t* data = img.data.data();
    
    // parallelize with static scheduling
    #pragma omp parallel for schedule(static)
    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            float gx = 0.0f, gy = 0.0f;
            
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    uint8_t pixel = data[(y + ky) * w + (x + kx)];
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


// Non-Maximum Suppression (OpenMP)
Image nonMaxSuppression(int width, int height, const vector<float>& magnitude, 
                        const vector<float>& direction) {
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
            
            // determine neighbors based on gradient direction
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
            
            // keep only local maximum
            if (mag >= neighbor1 && mag >= neighbor2) {
                suppressed.at(x, y) = static_cast<uint8_t>(min(mag, 255.0f));
            } else {
                suppressed.at(x, y) = 0;
            }
        }
    }
    
    return suppressed;
}


// recursive tracking (DFS) - run locally per process
void trackEdge(int x, int y, int width, int height, Image& result, vector<bool>& visited) {
    // check if position is invalid or already visited
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;

    int idx = y * width + x;

    if (visited[idx])
        return;

    // we only track from weak edges
    if (result.at(x, y) != 128)
        return;
    
    // promote weak edge to strong edge
    visited[idx] = true;
    result.at(x, y) = 255;
    
    // recurse to all 8 neighbors
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            trackEdge(x + dx, y + dy, width, height, result, visited);
        }
    }
}


// Double Threshold + Hysteresis (OpenMP)
Image hysteresisThreshold(const Image& img, int lowThreshold, int highThreshold) {
    Image result(img.width, img.height, 1);
    int w = img.width;
    int h = img.height;
    vector<bool> visited(w * h, false);
    
    // parallel threshold marking
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t val = img.at(x, y);
            if (val >= highThreshold) {
                result.at(x, y) = 255;
            } else if (val >= lowThreshold) {
                result.at(x, y) = 128;
            } else {
                result.at(x, y) = 0;
            }
        }
    }

    // sequential edge tracking
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (result.at(x, y) == 255 && !visited[y * w + x]) {
                visited[y * w + x] = true;
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


// all the stages for Canny Edge
Image cannyEdgeDetectionLocal(const Image& input, int lowThreshold, int highThreshold,
                              int kernelSize, float sigma, vector<double>& timings) {
    
    // 1. Grayscale
    double t1 = MPI_Wtime();
    Image gray = Image::toGrayscale(input);
    double t2 = MPI_Wtime();
    timings[STAGE_GRAY] = t2 - t1;

    // 2. Gaussian Blur
    t1 = MPI_Wtime();
    Image blurred = Image::gaussianBlur(gray, kernelSize, sigma);
    t2 = MPI_Wtime();
    timings[STAGE_BLUR] = t2 - t1;
    
    // 3. Sobel
    t1 = MPI_Wtime();
    vector<float> magnitude, direction;
    sobelGradient(blurred, magnitude, direction);
    t2 = MPI_Wtime();
    timings[STAGE_SOBEL] = t2 - t1;
    
    // 4. NMS
    t1 = MPI_Wtime();
    Image suppressed = nonMaxSuppression(blurred.width, blurred.height, magnitude, direction);
    t2 = MPI_Wtime();
    timings[STAGE_NMS] = t2 - t1;
    
    // 5. Hysteresis
    t1 = MPI_Wtime();
    Image edges = hysteresisThreshold(suppressed, lowThreshold, highThreshold);
    t2 = MPI_Wtime();
    timings[STAGE_HYST] = t2 - t1;
    
    return edges;
}



int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 3) {
        if (rank == 0) {
            cout << "Usage: " << argv[0] << " <input_image> <output_image.png> "
                 << "[low_threshold] [high_threshold] [kernel_size] [sigma] [num_threads]\n";
        }
        MPI_Finalize();
        return -1;
    }

    string inputPath = argv[1];
    string outputPath = argv[2];
    
    int lowThreshold = 15;
    int highThreshold = 40;
    int kernelSize = 9;
    float sigma = 1.5f;
    int numThreads = 0; // 0 = auto-detect

    if (argc >= 4) lowThreshold = stoi(argv[3]);
    if (argc >= 5) highThreshold = stoi(argv[4]);
    if (argc >= 6) kernelSize = stoi(argv[5]);
    if (argc >= 7) sigma = stof(argv[6]);
    if (argc >= 8) numThreads = stoi(argv[7]);

    // set thread count
    if (numThreads > 0) {
        omp_set_num_threads(numThreads);
    }

    Image input;
    int width, height, channels;

    // start total time
    double tStartTotal = MPI_Wtime();

    // rank 0: load image
    if (rank == 0) {
        int w, h, c;
        uint8_t* imageData = stbi_load(inputPath.c_str(), &w, &h, &c, 0);
        
        if (!imageData) {
            cerr << "Error loading image: " << stbi_failure_reason() << "\n";
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
        
        input = Image(w, h, c);
        copy(imageData, imageData + w * h * c, input.data.begin());
        stbi_image_free(imageData);
        
        width = w;
        height = h;
        channels = c;
    }


    // broadcast image dimensions
    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&channels, 1, MPI_INT, 0, MPI_COMM_WORLD);


    // compute strip distribution
    int rowsPerProc = height / size;
    int extraRows = height % size;

    int startRow = rank * rowsPerProc + min(rank, extraRows);
    int endRow = startRow + rowsPerProc + (rank < extraRows ? 1 : 0);
    int localRows = endRow - startRow;

    // allocate local image buffer
    Image localImg(width, localRows, channels);

    // scatter image data
    vector<int> sendcounts(size), displs(size);
    if (rank == 0) {
        int offset = 0;
        for (int i = 0; i < size; i++) {
            int rows = height / size + (i < extraRows ? 1 : 0);
            sendcounts[i] = rows * width * channels;
            displs[i] = offset;
            offset += rows * width * channels;
        }
    }

    MPI_Scatterv(input.data.data(), sendcounts.data(), displs.data(), MPI_UNSIGNED_CHAR,
                 localImg.data.data(), localRows * width * channels, MPI_UNSIGNED_CHAR,
                 0, MPI_COMM_WORLD);

    
    // array for storing the time of each process
    vector<double> localTimings(STAGE_COUNT, 0.0);

    Image localEdges = cannyEdgeDetectionLocal(localImg, lowThreshold, highThreshold,
                                             kernelSize, sigma, localTimings);


    // gather results
    vector<int> recvcounts(size), recvdispls(size);
    if (rank == 0) {
        int offset = 0;
        for (int i = 0; i < size; i++) {
            int rows = height / size + (i < extraRows ? 1 : 0);
            // 1 channel after processing
            recvcounts[i] = rows * width;
            recvdispls[i] = offset;
            offset += rows * width;
        }
    }

    Image finalEdges(width, height, 1);
    MPI_Gatherv(localEdges.data.data(), localRows * width, MPI_UNSIGNED_CHAR,
                finalEdges.data.data(), recvcounts.data(), recvdispls.data(),
                MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    // end total time
    double tEndTotal = MPI_Wtime();

    if (rank == 0) {
        cout << "========================================\n";
        cout << "Canny Edge Detection Completed\n";
        cout << "Image Size: " << width << "x" << height << "\n";
        cout << "MPI Ranks: " << size << ", Threads/Rank: " << (numThreads > 0 ? numThreads : omp_get_max_threads()) << "\n";
        cout << "Total Pipeline Time: " << fixed << setprecision(4) << (tEndTotal - tStartTotal) << " s\n";
        cout << "----------------------------------------\n";
        cout << "Breakdown (Rank 0 timings):\n";
        cout << "  Grayscale:      " << localTimings[STAGE_GRAY] << " s\n";
        cout << "  Gaussian blur:  " << localTimings[STAGE_BLUR] << " s\n";
        cout << "  Sobel:          " << localTimings[STAGE_SOBEL] << " s\n";
        cout << "  NMS:            " << localTimings[STAGE_NMS] << " s\n";
        cout << "  Hysteresis:     " << localTimings[STAGE_HYST] << " s\n";
        cout << "========================================\n";

        // saving image
        if (!stbi_write_png(outputPath.c_str(), width, height, 1, 
                            finalEdges.data.data(), width)) {
            cerr << "Error saving image\n";
            MPI_Finalize();
            return -1;
        }

        // salculations for edge percentage
        int totalPixels = finalEdges.width * finalEdges.height;
        int edgePixels = 0;
        
        #pragma omp parallel for reduction(+:edgePixels)
        for (int i = 0; i < finalEdges.data.size(); i++) {
            if (finalEdges.data[i] > 0)
                edgePixels++;
        }

        double edgePercentage = (edgePixels * 100.0) / totalPixels;
        
        cout << "\nStatistics:\n";
        cout << "   Total pixels: " << totalPixels << "\n";
        cout << "   Edge pixels:  " << edgePixels << " (" << fixed << setprecision(5) << edgePercentage << "%)\n";
    }

    MPI_Finalize();
    return 0;
}
