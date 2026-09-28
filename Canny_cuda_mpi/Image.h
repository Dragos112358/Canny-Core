#include <cstdint>
#include <vector>

using namespace std;

struct Image {
    int width;
    int height;
    int channels;
    vector<uint8_t> data;
    
    Image();
    Image(int w, int h, int c);
    
    uint8_t& at(int x, int y, int c = 0);
    const uint8_t& at(int x, int y, int c = 0) const;
};

// CUDA kernel declarations
__global__ void toGrayscaleKernel(const uint8_t* rgb, uint8_t* gray, int width, int height, int channels);
__global__ void createGaussianKernel(float *kernel, int size, float sigma);
__global__ void gaussianBlurKernel(const uint8_t* input, uint8_t* output, int width, int height, 
                                   const float* kernel, int kernelSize);

// Helper function to create Gaussian kernel on GPU
float* createAndGetGaussianKernelGPU(int size, float sigma); 

