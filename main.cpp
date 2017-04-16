// http://paulbourke.net/miscellaneous/imagefilter/

#include <iostream>
#include <functional>
#include <map>
#include <memory>
#include <chrono>
#include <vector>
#include <stdint.h>
#include <complex>
#include <type_traits>
#include "util.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "third-party/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third-party/stb/stb_image_write.h"

#include "kissfft/kissfft.hpp"

/* todo
 * [ ] image pyramid for mips, generate mips, ui for mips
 * [ ] support rgb textures
 */

inline void draw_text(int x, int y, const char * text)
{
    char buffer[64000];
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, 4 * stb_easy_font_print((float)x, (float)(y - 7), (char *)text, nullptr, buffer, sizeof(buffer)));
    glDisableClientState(GL_VERTEX_ARRAY);
}

class texture_buffer
{
    GLuint tex;
public:
    int2 size;
    texture_buffer() : tex(-1)
    {
        if (!tex) glGenTextures(1, &tex);
        glTextureParameteriEXT(tex, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteriEXT(tex, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteriEXT(tex, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTextureParameteriEXT(tex, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
    ~texture_buffer() { if (tex) glDeleteBuffers(1, &tex); };
    GLuint handle() const { return tex; }
};

template <typename T, int C>
struct image_buffer
{
    std::shared_ptr<T> data;
    const int2 size;
    T * alias;
    struct delete_array { void operator()(T const * p) { delete[] p; } };
    image_buffer() : size({ 0, 0 }) { }
    image_buffer(const int2 size) : size(size), data(new T[size.x * size.y * C], delete_array()) { alias = data.get(); }
    int size_bytes() const { return C * size.x * size.y * sizeof(T); }
    int num_pixels() const { return size.x * size.y; }
    T & operator()(int y, int x) { return alias[y * size.x + x]; }
    T & operator()(int y, int x, int channel) { return alias[C * (y * size.x + x) + channel]; }
    T compute_mean() const
    {
        T m = 0.0f;
        for (int x = 0; x < size.x * size.y; ++x) m += alias[x];
        return m / (size.x * size.y);
    }
};

inline void upload_png(texture_buffer & buffer, std::vector<uint8_t> & binaryData, bool flip = false)
{
    if (flip) stbi_set_flip_vertically_on_load(1);
    else stbi_set_flip_vertically_on_load(0);

    int width, height, nBytes;
    auto data = stbi_load_from_memory(binaryData.data(), (int)binaryData.size(), &width, &height, &nBytes, 0);

    switch (nBytes)
    {
    case 3: glTextureImage2DEXT(buffer.handle(), GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data); break;
    case 4: glTextureImage2DEXT(buffer.handle(), GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data); break;
    default: throw std::runtime_error("unsupported number of channels");
    }
    stbi_image_free(data);
    buffer.size = { width, height };
}

inline void upload_dds(texture_buffer & buffer, std::vector<uint8_t> & binaryData)
{
    gli::texture t(gli::load_dds((char *)binaryData.data(), binaryData.size()));

    for (std::size_t l = 0; l < t.levels(); ++l)
    {
        GLsizei w = (t.extent(l).x), h = (t.extent(l).y);
        gli::gl GL(gli::gl::PROFILE_GL33);
        gli::gl::format const Format = GL.translate(t.format(), t.swizzles());
        GLenum Target = GL.translate(t.target());
        glCompressedTextureImage2DEXT(buffer.handle(), Target, GLint(l), Format.Internal, w, h, 0, t.size(l), t.data(0, 0, l));
        if (l == 0) buffer.size = { w, h };
    }
}

image_buffer<float, 1> png_to_luminance(std::vector<uint8_t> & binaryData)
{
    int width, height, nBytes;
    auto data = stbi_load_from_memory(binaryData.data(), (int)binaryData.size(), &width, &height, &nBytes, 0);
 
    image_buffer<float, 1> buffer({ width, height });

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; ++x)
        {
            const float r = as_float<uint8_t>(data[nBytes * (y * width + x) + 0]);
            const float g = as_float<uint8_t>(data[nBytes * (y * width + x) + 1]);
            const float b = as_float<uint8_t>(data[nBytes * (y * width + x) + 2]);
            buffer(y, x) = to_luminance(r, g, b);
        }
    }
    stbi_image_free(data);
    return buffer;
}

void upload_luminance(texture_buffer & buffer, image_buffer<float, 1> & imgData)
{
    glTextureImage2DEXT(buffer.handle(), GL_TEXTURE_2D, 0, GL_LUMINANCE, imgData.size.x, imgData.size.y, 0, GL_LUMINANCE, GL_FLOAT, imgData.data.get());
}

void draw_texture_buffer(float rx, float ry, float rw, float rh, const texture_buffer & buffer)
{
    glBindTexture(GL_TEXTURE_2D, buffer.handle());
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(rx, ry);
    glTexCoord2f(1, 0); glVertex2f(rx + rw, ry);
    glTexCoord2f(1, 1); glVertex2f(rx + rw, ry + rh);
    glTexCoord2f(0, 1); glVertex2f(rx, ry + rh);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void center_fft_image(image_buffer<float, 1> & in, image_buffer<float, 1> & out)
{
    assert(in.size == out.size);

    const int halfWidth = in.size.x / 2;
    const int halfHeight = in.size.y / 2;

    for (int i = 0; i < in.size.y; i++)
    {
        for (int j = 0; j < in.size.x; j++)
        {
            if (i < halfHeight)
            {
                if (j < halfWidth) out(i, j) = in(i + halfHeight, j + halfWidth);
                else out(i, j) = in(i + halfWidth, j - halfWidth);
            }
            else 
            {
                if (j < halfWidth) out(i, j) = in(i - halfHeight, j + halfWidth);
                else out(i, j) = in(i - halfHeight, j - halfWidth);
            }
        }
    }
}

// In place
void compute_fft_2d(std::complex<float> * data, const int2 & size, const bool inverse = false) 
{
    const int width = size.x;
    const int height = size.y;

    kissfft<float> xFFT(width, inverse);
    kissfft<float> yFFT(height, inverse);

    std::vector<std::complex<float>> xTmp(std::max(width, height));
    std::vector<std::complex<float>> yTmp(std::max(width, height));
    std::vector<std::complex<float>> ySrc(height);

    // Compute FFT on X axis
    for (int y = 0; y < height; ++y)
    {
        const std::complex<float> * inputRow = &data[y * width];
        xFFT.transform(inputRow, xTmp.data());
        for (int x = 0; x < width; x++) data[y * width + x] = xTmp[x];
    }

    // Compute FFT on Y axis
    for (int x = 0; x < width; x++)
    {
        // For data locality, create a 1d src "row" out of the Y column
        for (int y = 0; y < height; y++) ySrc[y] = data[y * width + x];
        yFFT.transform(ySrc.data(), yTmp.data());
        for (int y = 0; y < height; y++) data[y * width + x] = yTmp[y];
    }
}

//////////////////////////
//   Main Application   //
//////////////////////////

std::unique_ptr<texture_buffer> loadedTexture;
std::unique_ptr<Window> win;

/*
    std::vector<int2> pyramidSizes;
    build_pyramid_dimensions(pyramidSizes, 512);
    for (auto s : pyramidSizes) std::cout << s << std::endl;
*/

template <typename T, int C>
struct image_buffer_pyramid
{
    image_buffer<T, C> & level(const int level)
    {
        assert(level < pyramid.size());
        return pyramid[level];
    }
    std::vector<image_buffer<T, C>> pyramid;
};

inline void build_pyramid_dimensions(std::vector<int2> & dimensions, int size)
{
    if (size == 2)
    {
        dimensions.push_back({ 1, 1 });
        return;
    }
    dimensions.push_back({ size, size});
    build_pyramid_dimensions(dimensions, size / 2);
}

int main(int argc, char * argv[])
{
    image_buffer_pyramid<float, 1> pyramid;

    std::string status("No file currently loaded...");

    try
    {
        win.reset(new Window(512, 512, "image fft visualizer"));
    }
    catch (const std::exception & e)
    {
        std::cout << "Caught GLFW window exception: " << e.what() << std::endl;
    }

    win->on_drop = [&](int numFiles, const char ** paths)
    {
        for (int f = 0; f < numFiles; f++)
        {
            std::vector<uint8_t> data;
            loadedTexture.reset(new texture_buffer()); // gen handle
            const std::string fileExtension = get_extension(paths[f]);
            status = paths[f];

            try
            {
                data = read_file_binary(std::string(paths[f]));
            }
            catch (const std::exception & e)
            {
                status = std::string("Couldn't read file: ") + e.what();
            }

            if (fileExtension == "png" || fileExtension == "PNG")
            {
                auto img = png_to_luminance(data);

                if (!is_power_of_two(img.size.x) || !is_power_of_two(img.size.y))
                {
                    status = "Image size is not a power of two";
                    return;
                }

                // Resize window
                int2 existingWindowSize = win->get_window_size();
                int2 newWindowSize = int2(std::max(existingWindowSize.x, img.size.x), std::max(existingWindowSize.y, img.size.y));
                win->set_window_size(newWindowSize);

                float mean = img.compute_mean();
                std::vector<std::complex<float>>imgAsComplexArray(img.size.x * img.size.y);

                for (int y = 0; y < img.size.y; y++)
                    for (int x = 0; x < img.size.x; x++)
                        imgAsComplexArray[y * img.size.x + x] = img(y, x) - mean;

                compute_fft_2d(imgAsComplexArray.data(), img.size);

                float min = std::abs(imgAsComplexArray[0]), max = min;
                for (int i = 0; i < img.size.x * img.size.y; i++) 
                {
                    float value = std::abs(imgAsComplexArray[i]);
                    min = std::min(min, value);
                    max = std::max(max, value);
                }

                // Convert back to image type & normalize range
                for (int y = 0; y < img.size.y; y++)
                {
                    for (int x = 0; x < img.size.x; x++)
                    {
                        const auto v = imgAsComplexArray[y * img.size.x + x];
                        img(y, x) = ((std::sqrt((v.real() * v.real()) + (v.imag() * v.imag())) - min) / (max - min)) * 64.f;
                    }
                }

                // Move zero-frequency to the center
                image_buffer<float, 1> centered(img.size);
                center_fft_image(img, centered);

                loadedTexture->size = { img.size.x, img.size.y };
                upload_luminance(*loadedTexture.get(), centered);
            }
            else if (fileExtension == "dds")
            {
                upload_dds(*loadedTexture.get(), data);
            }
            else
            {
                status = "Unsupported file format";
            }
        }
    };

    auto t0 = std::chrono::high_resolution_clock::now();
    while (!win->should_close())
    {
        glfwPollEvents();

        auto t1 = std::chrono::high_resolution_clock::now();
        float timestep = std::chrono::duration<float>(t1 - t0).count();
        t0 = t1;

        auto windowSize = win->get_window_size();
        glViewport(0, 0, windowSize.x, windowSize.y);
        glClear(GL_COLOR_BUFFER_BIT);

        glPushMatrix();

        glOrtho(0, windowSize.x, windowSize.y, 0, -1, +1);

        if (loadedTexture.get())
        {
            draw_texture_buffer(0, 0, loadedTexture->size.x, loadedTexture->size.y, *loadedTexture.get());
        }

        draw_text(10, 16, status.c_str());

        glPopMatrix();

        win->swap_buffers();
    }
    return EXIT_SUCCESS;
}
