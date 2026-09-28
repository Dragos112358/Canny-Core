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

    // Processing operations
    static Image toGrayscale(const Image& img);

    // Declaration for the new overload
    static Image gaussianBlur(const Image& img, int kernelSize, float sigma);

    // Declaration for the optimized 9x9 blur
    static Image gaussianBlur(const Image& img);

    // Missing declaration (fix for the error)
    static vector<float> createGaussianKernel(int size, float sigma);

};
struct ThreadArgs {
    int id, totalThreads;
    const Image* input;
    Image* output;
};

struct BlurArgs {
    int id, totalThreads;
    const Image* input;
    Image* output;
    const vector<float>* kernel;
    int kernelSize;
};

struct SobelArgs {
    int id, totalThreads;
    const Image* img;
    vector<float>* magnitude;
    vector<float>* direction;
};
Image toGrayscalePthreads(const Image& img, int numThreads);
Image gaussianBlurPthreads(const Image& img, int kernelSize, float sigma, int numThreads);
