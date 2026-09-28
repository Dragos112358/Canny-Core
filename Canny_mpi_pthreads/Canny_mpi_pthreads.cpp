#include <mpi.h>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iomanip> // Pentru formatare afisare timpi

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace std;

// ==================== CONFIGURARE ====================
int g_num_threads = 4;
pthread_barrier_t g_barrier;

// Index pentru array-ul de timpi
enum Stage {
    STAGE_GRAY = 0,
    STAGE_BLUR_H,
    STAGE_BLUR_V,
    STAGE_SOBEL,
    STAGE_NMS,
    STAGE_HYST,
    STAGE_COUNT
};

struct PipelineContext {
    uint8_t* rawInput;
    uint8_t* gray;
    uint8_t* blurTmp;
    uint8_t* blurDst;
    float* mag;
    float* dir;
    uint8_t* nms;
    uint8_t* edges;

    int width;
    int totalHeight;
    int coreHeight;
    int ghostTop;

    int lowThresh;
    int highThresh;
    int kernelSize;
    float sigma;
    vector<float> kernel;

    // Array pentru a stoca timpii (in secunde)
    double timings[STAGE_COUNT];
};

struct ThreadArgs {
    int id;
    PipelineContext* ctx;
    int startRow;
    int endRow;
};

// ==================== CALCULE AUXILIARE ====================

vector<float> createGaussianKernel(int size, float sigma) {
    vector<float> k(size);
    float sum = 0.0f;
    int half = size / 2;
    for (int i = -half; i <= half; i++) {
        k[i + half] = exp(-(i * i) / (2.0f * sigma * sigma));
        sum += k[i + half];
    }
    for (float& x : k) x /= sum;
    return k;
}

// ==================== WORKER THREAD ====================

void* workerThread(void* arg) {
    ThreadArgs* t = (ThreadArgs*)arg;
    PipelineContext* ctx = t->ctx;
    int w = ctx->width;
    int h = ctx->totalHeight;
    int id = t->id;
    
    // Variabila locala pentru timing (doar thread 0 scrie in context)
    double t_start = 0.0;
    if (id == 0) t_start = MPI_Wtime();

    // 1. Grayscale
    for (int y = t->startRow; y < t->endRow; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            uint8_t r = ctx->rawInput[idx * 3 + 0];
            uint8_t g = ctx->rawInput[idx * 3 + 1];
            uint8_t b = ctx->rawInput[idx * 3 + 2];
            ctx->gray[idx] = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        }
    }
    pthread_barrier_wait(&g_barrier);
    if (id == 0) {
        double now = MPI_Wtime();
        ctx->timings[STAGE_GRAY] = now - t_start;
        t_start = now;
    }

    // 2. Blur Horizontal
    int kSize = ctx->kernelSize;
    int half = kSize / 2;
    const float* kern = ctx->kernel.data();

    for (int y = t->startRow; y < t->endRow; y++) {
        for (int x = 0; x < w; x++) {
            float sum = 0.0f;
            for (int k = -half; k <= half; k++) {
                int px = min(max(x + k, 0), w - 1);
                sum += ctx->gray[y * w + px] * kern[k + half];
            }
            ctx->blurTmp[y * w + x] = (uint8_t)sum;
        }
    }
    pthread_barrier_wait(&g_barrier);
    if (id == 0) {
        double now = MPI_Wtime();
        ctx->timings[STAGE_BLUR_H] = now - t_start;
        t_start = now;
    }

    // 3. Blur Vertical
    for (int y = t->startRow; y < t->endRow; y++) {
        for (int x = 0; x < w; x++) {
            float sum = 0.0f;
            for (int k = -half; k <= half; k++) {
                int py = min(max(y + k, 0), h - 1);
                sum += ctx->blurTmp[py * w + x] * kern[k + half];
            }
            ctx->blurDst[y * w + x] = (uint8_t)sum;
        }
    }
    pthread_barrier_wait(&g_barrier);
    if (id == 0) {
        double now = MPI_Wtime();
        ctx->timings[STAGE_BLUR_V] = now - t_start;
        t_start = now;
    }

    int marginSobel = half + 1;

    // 4. Sobel
    for (int y = t->startRow; y < t->endRow; y++) {
        if (y < marginSobel || y >= h - marginSobel) continue;

        for (int x = 1; x < w - 1; x++) {
            float gx = 0, gy = 0;
            
            gx = ctx->blurDst[(y-1)*w + (x+1)] + 2*ctx->blurDst[y*w + (x+1)] + ctx->blurDst[(y+1)*w + (x+1)] -
                 (ctx->blurDst[(y-1)*w + (x-1)] + 2*ctx->blurDst[y*w + (x-1)] + ctx->blurDst[(y+1)*w + (x-1)]);
            
            gy = ctx->blurDst[(y-1)*w + (x-1)] + 2*ctx->blurDst[(y-1)*w + x] + ctx->blurDst[(y-1)*w + (x+1)] -
                 (ctx->blurDst[(y+1)*w + (x-1)] + 2*ctx->blurDst[(y+1)*w + x] + ctx->blurDst[(y+1)*w + (x+1)]);

            int idx = y * w + x;
            ctx->mag[idx] = sqrtf(gx*gx + gy*gy);
            ctx->dir[idx] = atan2f(gy, gx);
        }
    }
    pthread_barrier_wait(&g_barrier);
    if (id == 0) {
        double now = MPI_Wtime();
        ctx->timings[STAGE_SOBEL] = now - t_start;
        t_start = now;
    }

    int marginNMS = marginSobel + 1;

    // 5. NMS (Non-Maximum Suppression)
    for (int y = t->startRow; y < t->endRow; y++) {
        if (y < marginNMS || y >= h - marginNMS) continue;

        for (int x = 1; x < w - 1; x++) {
            int idx = y * w + x;
            float angle = ctx->dir[idx] * 180.0f / M_PI;
            if (angle < 0) angle += 180.0f;

            float q = ctx->mag[idx];
            float r = 0, s = 0;

            if ((angle >= 0 && angle < 22.5) || (angle >= 157.5 && angle <= 180)) {
                r = ctx->mag[idx - 1]; 
                s = ctx->mag[idx + 1];
            } 
            else if (angle >= 22.5 && angle < 67.5) {
                r = ctx->mag[idx - w - 1]; 
                s = ctx->mag[idx + w + 1]; 
            } 
            else if (angle >= 67.5 && angle < 112.5) {
                r = ctx->mag[idx - w]; 
                s = ctx->mag[idx + w];
            } 
            else if (angle >= 112.5 && angle < 157.5) {
                r = ctx->mag[idx - w + 1]; 
                s = ctx->mag[idx + w - 1]; 
            }

            if (q >= r && q >= s) ctx->nms[idx] = (uint8_t)min(q, 255.0f);
            else ctx->nms[idx] = 0;
        }
    }
    pthread_barrier_wait(&g_barrier);
    
    // 6. Double Thresholding (Clasificare inițială)
    for (int y = t->startRow; y < t->endRow; y++) {
        if (y < marginNMS || y >= h - marginNMS) continue;

        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            uint8_t val = ctx->nms[idx];
            
            if (val >= ctx->highThresh) ctx->edges[idx] = 255;
            else if (val >= ctx->lowThresh) ctx->edges[idx] = 128;
            else ctx->edges[idx] = 0;
        }
    }

    pthread_barrier_wait(&g_barrier);
    if (id == 0) {
        double now = MPI_Wtime();
        // Aici includem NMS + Double Threshold
        ctx->timings[STAGE_NMS] = now - t_start; 
    }

    return nullptr;
}

// ==================== HYSTERESIS ITERATIV ====================
void performHysteresis(int startX, int startY, int w, int h, uint8_t* data) {
    vector<pair<int, int>> stack;
    stack.push_back({startX, startY});

    while (!stack.empty()) {
        pair<int, int> pos = stack.back();
        stack.pop_back();
        int cx = pos.first;
        int cy = pos.second;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;

                int nx = cx + dx;
                int ny = cy + dy;

                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    int nidx = ny * w + nx;
                    if (data[nidx] == 128) {
                        data[nidx] = 255;
                        stack.push_back({nx, ny});
                    }
                }
            }
        }
    }
}

// ==================== FUNCTIA NOUA: CANNY EDGE DETECTION ====================
void CannyEdgeDetection(PipelineContext& ctx, int numThreads) {
    
    // 1. Initializare Bariera
    pthread_barrier_init(&g_barrier, nullptr, numThreads);
    
    vector<pthread_t> threads(numThreads);
    vector<ThreadArgs> tArgs(numThreads);

    int tRows = ctx.totalHeight / numThreads;

    // 2. Lansare Thread-uri pentru (Gray -> Blur -> Sobel -> NMS)
    for(int i=0; i<numThreads; i++) {
        tArgs[i].id = i;
        tArgs[i].ctx = &ctx;
        tArgs[i].startRow = i * tRows;
        tArgs[i].endRow = (i == numThreads-1) ? ctx.totalHeight : (i+1)*tRows;
        pthread_create(&threads[i], nullptr, workerThread, &tArgs[i]);
    }

    // 3. Join Thread-uri
    for(int i=0; i<numThreads; i++) {
        pthread_join(threads[i], nullptr);
    }
    pthread_barrier_destroy(&g_barrier);

    // 4. Hysteresis (Serial pe Rank)
    double tHystStart = MPI_Wtime();
    int width = ctx.width;
    int height = ctx.totalHeight;

    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            int idx = y * width + x;
            if (ctx.edges[idx] == 255) {
                performHysteresis(x, y, width, height, ctx.edges);
            }
        }
    }

    // Curatare pixeli slabi neconectati
    for(int i = 0; i < width * height; i++) {
        if (ctx.edges[i] == 128) ctx.edges[i] = 0;
    }
    
    double tHystEnd = MPI_Wtime();
    ctx.timings[STAGE_HYST] = tHystEnd - tHystStart;
}

// ==================== MAIN ====================

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 3) {
        if(rank==0) cout << "Usage: ./exe input output [low] [high] [ksize] [sigma] [threads]\n";
        MPI_Finalize(); return -1;
    }

    string inputPath = argv[1];
    string outputPath = argv[2];
    int lowThresh = 15;
    int highThresh = 45;
    int kernelSize = 9; 
    float sigma = 1.5f;
    if (argc >= 8) g_num_threads = stoi(argv[7]);

    int width, height, channels;
    vector<uint8_t> fullImgData;

    // --- 1. Încărcare Imagine (Doar Rank 0) ---
    if (rank == 0) {
        int w, h, c;
        uint8_t* data = stbi_load(inputPath.c_str(), &w, &h, &c, 3); 
        if (!data) { 
            cerr << "Failed to load image.\n";
            MPI_Abort(MPI_COMM_WORLD, -1); 
        }
        width = w; height = h; channels = 3;
        fullImgData.assign(data, data + w*h*3);
        stbi_image_free(data);
    }

    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&channels, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // --- 2. Distribuire Date (Scatter) ---
    int haloNeeded = (kernelSize / 2) + 2;
    int rowsPerRank = height / size;
    int remainder = height % size;
    
    vector<int> sendCounts(size), displs(size);
    int myTotalHeight = 0; 
    int myGhostTop = 0;

    if (rank == 0) {
        int currentY = 0;
        for (int i = 0; i < size; i++) {
            int r = rowsPerRank + (i < remainder ? 1 : 0);
            int startY = max(0, currentY - haloNeeded);
            int endY = min(height, currentY + r + haloNeeded);
            
            sendCounts[i] = (endY - startY) * width * channels;
            displs[i] = startY * width * channels;
            currentY += r;
        }
    }

    int myCoreRows = rowsPerRank + (rank < remainder ? 1 : 0);
    int myCoreStartGlobal = 0;
    for(int i=0; i<rank; i++) myCoreStartGlobal += rowsPerRank + (i < remainder ? 1 : 0);
    int myStartGlobal = max(0, myCoreStartGlobal - haloNeeded);
    int myEndGlobal = min(height, myCoreStartGlobal + myCoreRows + haloNeeded);
    
    myTotalHeight = myEndGlobal - myStartGlobal;
    myGhostTop = myCoreStartGlobal - myStartGlobal;

    vector<uint8_t> localRaw(myTotalHeight * width * channels);
    MPI_Scatterv(fullImgData.data(), sendCounts.data(), displs.data(), MPI_UNSIGNED_CHAR,
                 localRaw.data(), myTotalHeight * width * channels, MPI_UNSIGNED_CHAR,
                 0, MPI_COMM_WORLD);

    // --- 3. Pregatire Context si Apel Functie Procesare ---
    PipelineContext ctx;
    ctx.rawInput = localRaw.data();
    ctx.width = width;
    ctx.totalHeight = myTotalHeight;
    ctx.coreHeight = myCoreRows;
    ctx.ghostTop = myGhostTop;
    ctx.lowThresh = lowThresh;
    ctx.highThresh = highThresh;
    ctx.kernelSize = kernelSize;
    ctx.sigma = sigma;
    ctx.kernel = createGaussianKernel(kernelSize, sigma);

    // Alocare buffere locale
    int pixelCount = width * myTotalHeight;
    vector<uint8_t> bufGray(pixelCount, 0), bufBlurTmp(pixelCount, 0), bufBlurDst(pixelCount, 0);
    vector<float> bufMag(pixelCount, 0.0f), bufDir(pixelCount, 0.0f);
    vector<uint8_t> bufNms(pixelCount, 0), bufEdges(pixelCount, 0);

    ctx.gray = bufGray.data();
    ctx.blurTmp = bufBlurTmp.data();
    ctx.blurDst = bufBlurDst.data();
    ctx.mag = bufMag.data();
    ctx.dir = bufDir.data();
    ctx.nms = bufNms.data();
    ctx.edges = bufEdges.data();

    // Resetare timpi
    for(int i=0; i<STAGE_COUNT; i++) ctx.timings[i] = 0.0;

    double tStartTotal = MPI_Wtime();

    // ============================================
    // APELAREA NOII FUNCTII
    // ============================================
    CannyEdgeDetection(ctx, g_num_threads);

    double tEndTotal = MPI_Wtime();

    // --- 4. Gather si Salvare (Rank 0) ---
    vector<uint8_t> finalImg;
    if(rank == 0) finalImg.resize(width * height);

    vector<int> recvCounts(size), recvDispls(size);
    if(rank == 0) {
        int cur = 0;
        for(int i=0; i<size; i++) {
            int r = rowsPerRank + (i < remainder ? 1 : 0);
            recvCounts[i] = r * width;
            recvDispls[i] = cur * width;
            cur += r;
        }
    }

    uint8_t* myCoreData = ctx.edges + (myGhostTop * width);
    MPI_Gatherv(myCoreData, myCoreRows * width, MPI_UNSIGNED_CHAR,
                finalImg.data(), recvCounts.data(), recvDispls.data(), MPI_UNSIGNED_CHAR,
                0, MPI_COMM_WORLD);

    // --- 5. Afisare Statistici si Salvare ---
    if(rank == 0) {
        cout << "========================================\n";
        cout << "Canny Edge Detection Completed\n";
        cout << "Image Size: " << width << "x" << height << "\n";
        cout << "MPI Ranks: " << size << ", Threads/Rank: " << g_num_threads << "\n";
        cout << "Total Pipeline Time: " << fixed << setprecision(4) << (tEndTotal - tStartTotal) << " s\n";
        cout << "----------------------------------------\n";
        cout << "Breakdown (Rank 0 timings):\n";
        cout << "  Grayscale:   " << ctx.timings[STAGE_GRAY] << " s\n";
        cout << "  Gaussian blur:    " << ctx.timings[STAGE_BLUR_H] + ctx.timings[STAGE_BLUR_V]<< " s\n";
        cout << "  Sobel:       " << ctx.timings[STAGE_SOBEL] << " s\n";
        cout << "  NMS + Thresh:" << ctx.timings[STAGE_NMS] << " s\n";
        cout << "  Hysteresis:  " << ctx.timings[STAGE_HYST] << " s\n";
        cout << "========================================\n";

        stbi_write_png(outputPath.c_str(), width, height, 1, finalImg.data(), width);
        
        // Calcul procentaj muchii
        long totalP = 0, edgeP = 0;
        for(auto p : finalImg) {
            totalP++;
            if(p > 0) edgeP++;
        }
        cout << "Edges found: " << edgeP << " (" << (100.0*edgeP/totalP) << "% of pixels)\n";
    }

    MPI_Finalize();
    return 0;
}