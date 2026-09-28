#include <mpi.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace std;


// Sobel Gradient
void sobelGradient(const Image& img, vector<float>& magnitude, vector<float>& direction) {
    magnitude.resize(img.width * img.height);
    direction.resize(img.width * img.height);

    const int Gx[3][3] = { {-1,0,1},{-2,0,2},{-1,0,1} };
    const int Gy[3][3] = { {-1,-2,-1},{0,0,0},{1,2,1} };

    const uint8_t* data = img.data.data();

    for (int y = 1; y < img.height - 1; y++) {
        for (int x = 1; x < img.width - 1; x++) {
            float gx = 0.0f, gy = 0.0f;
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    int idx = (y + ky) * img.width + (x + kx);
                    uint8_t pixel = data[idx];
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
    Image suppressed(width, height, 1);
    uint8_t* data = suppressed.data.data();

    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float mag = magnitude[idx];
            float angle = direction[idx] * 180.0f / M_PI;
            if (angle < 0) angle += 180.0f;

            float neighbor1 = 0, neighbor2 = 0;
            if ((angle >= 0 && angle < 22.5) || (angle >= 157.5 && angle < 180)) {
                neighbor1 = magnitude[idx - 1];
                neighbor2 = magnitude[idx + 1];
            } else if (angle >= 22.5 && angle < 67.5) {
                neighbor1 = magnitude[idx - width + 1];
                neighbor2 = magnitude[idx + width - 1];
            } else if (angle >= 67.5 && angle < 112.5) {
                neighbor1 = magnitude[idx - width];
                neighbor2 = magnitude[idx + width];
            } else {
                neighbor1 = magnitude[idx - width - 1];
                neighbor2 = magnitude[idx + width + 1];
            }

            data[idx] = (mag >= neighbor1 && mag >= neighbor2) ? static_cast<uint8_t>(min(mag, 255.0f)) : 0;
        }
    }
    return suppressed;
}

// Recursive tracking (DFS)
void trackEdge(int x, int y, int width, int height, Image& result, vector<bool>& visited) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    int idx = y * width + x;
    if (visited[idx]) return;
    if (result.at(x, y) != 128) return;
    
    visited[idx] = true;
    result.at(x, y) = 255;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            trackEdge(x + dx, y + dy, width, height, result, visited);
        }
    }
}

// Hysteresis Threshold
Image hysteresisThreshold(const Image& img, int lowThreshold, int highThreshold) {
    Image result = Image(img.width, img.height, 1);
    vector<bool> visited(img.width * img.height, false);
    
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            uint8_t val = img.at(x, y);
            if (val >= highThreshold) result.at(x, y) = 255;
            else if (val >= lowThreshold) result.at(x, y) = 128;
            else result.at(x, y) = 0;
        }
    }

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
            if (result.at(x, y) == 128) result.at(x, y) = 0;
        }
    }
    return result;
}


int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if(rank == 0) {
            cout << "Usage: " << argv[0] << " <input_image>\n";
        }
        MPI_Finalize();
        return -1;
    }

    string inputPath = argv[1];
    string outputPath = argv[2];
    Image input;

    int width, height, channels;
    if(rank == 0) {
        int w, h, c;
        uint8_t* imageData = stbi_load(inputPath.c_str(), &w, &h, &c, 0);
        if(!imageData) {
            cerr << "Error loading image\n";
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
        input = Image(w, h, c);
        copy(imageData, imageData + w * h * c, input.data.begin());
        stbi_image_free(imageData);
        width = w;
        height = h;
        channels = c;
    }

    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&channels, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int rowsPerProc = height / size;
    int extraRows = height % size;

    int startRow = rank * rowsPerProc + min(rank, extraRows);
    int endRow = startRow + rowsPerProc + (rank < extraRows ? 1 : 0);
    int localRows = endRow - startRow;

    Image localImg(width, localRows, channels);
    
    // scatter
    vector<int> sendcounts(size), displs(size);
    if(rank == 0) {
        int offset = 0;
        for(int i=0;i<size;i++){
            int rows = height/size + (i<extraRows?1:0);
            sendcounts[i] = rows * width * channels;
            displs[i] = offset;
            offset += rows * width * channels;
        }
    }
    

    double tStartTotal = MPI_Wtime();

    MPI_Scatterv(input.data.data(), sendcounts.data(), displs.data(), MPI_UNSIGNED_CHAR,
                 localImg.data.data(), localRows * width * channels, MPI_UNSIGNED_CHAR,
                 0, MPI_COMM_WORLD);


    double t1, t2;
    double time_gray, time_blur, time_sobel, time_nms, time_hyst;

    // convert to grayscale
    t1 = MPI_Wtime();
    Image gray = Image::toGrayscale(localImg);
    t2 = MPI_Wtime();
    time_gray = t2 - t1;

    // Gaussian blur
    t1 = MPI_Wtime();
    Image blurred = Image::gaussianBlur(gray, 9, 1.5f);
    t2 = MPI_Wtime();
    time_blur = t2 - t1;

    // Sobel
    t1 = MPI_Wtime();
    vector<float> magnitude, direction;
    sobelGradient(blurred, magnitude, direction);
    t2 = MPI_Wtime();
    time_sobel = t2 - t1;

    // Non-Max Suppression
    t1 = MPI_Wtime();
    Image suppressed = nonMaxSuppression(blurred.width, blurred.height, magnitude, direction);
    t2 = MPI_Wtime();
    time_nms = t2 - t1;

    // Hysteresis
    t1 = MPI_Wtime();
    Image edges = hysteresisThreshold(suppressed, 15, 45);
    t2 = MPI_Wtime();
    time_hyst = t2 - t1;


    vector<int> recvcounts(size), recvdispls(size);
    if(rank == 0){
        int offset = 0;
        for(int i=0;i<size;i++){
            int rows = height/size + (i<extraRows?1:0);
            recvcounts[i] = rows * width; 
            recvdispls[i] = offset;
            offset += rows * width;
        }
    }

    Image finalEdges(width, height, 1);
    MPI_Gatherv(edges.data.data(), localRows*width, MPI_UNSIGNED_CHAR,
                finalEdges.data.data(), recvcounts.data(), recvdispls.data(),
                MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    
    double tEndTotal = MPI_Wtime();

    if(rank == 0){
        stbi_write_png(outputPath.c_str(), width, height, 1, finalEdges.data.data(), width);
        
        cout << "MPI Canny completed!\n";
        cout << "Total elapsed time: " << tEndTotal - tStartTotal << " seconds\n";

        cout << "----------------------------------------\n";
        cout << "Breakdown (Rank 0 timings):\n";
        cout << "  Grayscale:      " << fixed << setprecision(4) << time_gray << " s\n";
        cout << "  Gaussian blur:  " << fixed << setprecision(4) << time_blur << " s\n";
        cout << "  Sobel:          " << fixed << setprecision(4) << time_sobel << " s\n";
        cout << "  NMS:            " << fixed << setprecision(4) << time_nms << " s\n";
        cout << "  Hysteresis:     " << fixed << setprecision(4) << time_hyst << " s\n";
        cout << "----------------------------------------\n";

        cout << "Output saved: " << outputPath << "\n";

        // percentage of edge pixels
        int totalPixels = finalEdges.width * finalEdges.height;
        int edgePixels = 0;
        for (uint8_t pixel : finalEdges.data) {
            if (pixel > 0)
                edgePixels++;
        }
        
        double edgePercentage = (edgePixels * 100.0) / totalPixels;
        cout << "Total pixels: " << totalPixels << "\n";
        cout << "Edge pixels: " << edgePixels << " (" << fixed << setprecision(2) << edgePercentage << "%)\n";
    }

    MPI_Finalize();
    return 0;
}
