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

    static Image toGrayscale(const Image& img);
    static vector<float> createGaussianKernel(int size, float sigma);
    static Image gaussianBlur(const Image& img, int kernelSize = 9, float sigma = 3.0f);
};

