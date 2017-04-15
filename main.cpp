#include <iostream>
#include <functional>
#include <map>
#include <memory>
#include <chrono>
#include <vector>
#include <stdint.h>
#include <complex>
#include <type_traits>

#include "linalg_util.hpp"

#define _CRT_SECURE_NO_WARNINGS

#include "gli/gli.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "third-party/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third-party/stb/stb_image_write.h"

#include "third-party/stb/stb_easy_font.h"

#define GLEW_STATIC
#define GL_GLEXT_PROTOTYPES
#include "glew.h"

#define GLFW_INCLUDE_GLU
#include "GLFW\glfw3.h"

#include "kissfft/kissfft.hpp"
#include "kissfft/kiss_fft.hpp"
#include "kissfft/kiss_fftr.hpp"

inline void draw_text(int x, int y, const char * text)
{
    char buffer[64000];
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, 4 * stb_easy_font_print((float)x, (float)(y - 7), (char *)text, nullptr, buffer, sizeof(buffer)));
    glDisableClientState(GL_VERTEX_ARRAY);
}

inline std::string get_extension(const std::string & path)
{
    auto found = path.find_last_of('.');
    if (found == std::string::npos) return "";
    else return path.substr(found + 1);
}

inline std::vector<uint8_t> read_file_binary(const std::string pathToFile)
{
    FILE * f = fopen(pathToFile.c_str(), "rb");

    if (!f) throw std::runtime_error("file not found");

    fseek(f, 0, SEEK_END);
    size_t lengthInBytes = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> fileBuffer(lengthInBytes);

    size_t elementsRead = fread(fileBuffer.data(), 1, lengthInBytes, f);

    if (elementsRead == 0 || fileBuffer.size() < 4) throw std::runtime_error("error reading file or file too small");

    fclose(f);
    return fileBuffer;
}

inline float to_luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722 * b;
}

template<class T> 
inline float as_float(const T & x)
{
    const float min = std::numeric_limits<T>::min();
    const float max = std::numeric_limits<T>::max();
    return (x - min) / (max - min);
}

class Window
{
    GLFWwindow * window;
public:
    std::function<void(unsigned int codepoint)> on_char;
    std::function<void(int key, int action, int mods)> on_key;
    std::function<void(int button, int action, int mods)> on_mouse_button;
    std::function<void(float2 pos)> on_cursor_pos;
    std::function<void(int numFiles, const char ** paths)> on_drop;

    Window(int width, int height, const char * title)
    {
        if (glfwInit() == GL_FALSE)
        {
            throw std::runtime_error("glfwInit() failed");
        }

        window = glfwCreateWindow(width, height, title, nullptr, nullptr);

        if (window == nullptr)
        {
            throw std::runtime_error("glfwCreateWindow() failed");
        }

        glfwMakeContextCurrent(window);

        if (GLenum err = glewInit())
        {
            throw std::runtime_error(std::string("glewInit() failed - ") + (const char *)glewGetErrorString(err));
        }

        glfwSetCharCallback(window, [](GLFWwindow * window, unsigned int codepoint) { 
            auto w = (Window *)glfwGetWindowUserPointer(window); if (w->on_char) w->on_char(codepoint); 
        });

        glfwSetKeyCallback(window, [](GLFWwindow * window, int key, int, int action, int mods) { 
            auto w = (Window *)glfwGetWindowUserPointer(window); if (w->on_key) w->on_key(key, action, mods); 
        });

        glfwSetMouseButtonCallback(window, [](GLFWwindow * window, int button, int action, int mods) { 
            auto w = (Window *)glfwGetWindowUserPointer(window); if (w->on_mouse_button) w->on_mouse_button(button, action, mods); 
        });

        glfwSetCursorPosCallback(window, [](GLFWwindow * window, double xpos, double ypos) { 
            auto w = (Window *)glfwGetWindowUserPointer(window); if (w->on_cursor_pos) w->on_cursor_pos(float2(double2(xpos, ypos))); 
        });

        glfwSetDropCallback(window, [](GLFWwindow * window, int numFiles, const char ** paths) { 
            auto w = (Window *)glfwGetWindowUserPointer(window); if (w->on_drop) w->on_drop(numFiles, paths); 
        });

        glfwSetWindowUserPointer(window, this);
    }

    ~Window()
    {
        glfwMakeContextCurrent(window);
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    Window(const Window &) = delete;
    Window(Window &&) = delete;
    Window & operator = (const Window &) = delete;
    Window & operator = (Window &&) = delete;

    GLFWwindow * get_glfw_window_handle() { return window; };
    bool should_close() const { return !!glfwWindowShouldClose(window); }
    int get_window_attrib(int attrib) const { return glfwGetWindowAttrib(window, attrib); }
    int2 get_window_size() const { int2 size; glfwGetWindowSize(window, &size.x, &size.y); return size; }
    int2 get_framebuffer_size() const { int2 size; glfwGetFramebufferSize(window, &size.x, &size.y); return size; }
    float2 get_cursor_pos() const { double2 pos; glfwGetCursorPos(window, &pos.x, &pos.y); return float2(pos); }

    void swap_buffers() { glfwSwapBuffers(window); }
    void close() { glfwSetWindowShouldClose(window, 1); }
};

class texture_buffer
{
    GLuint tex;
    int2 size;
public:

    texture_buffer() : tex(-1)
    {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    ~texture_buffer() { if (tex) glDeleteBuffers(1, &tex); }
    void set_size(int2 s) { size = s; }
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
    buffer.set_size({ width, height });
}

inline void upload_dds(texture_buffer & buffer, const gli::texture & t)
{
    for (std::size_t l = 0; l < t.levels(); ++l)
    {
        GLsizei w = (t.extent(l).x), h = (t.extent(l).y);
        std::cout << w << ", " << h << std::endl;
        gli::gl GL(gli::gl::PROFILE_GL33);
        gli::gl::format const Format = GL.translate(t.format(), t.swizzles());
        GLenum Target = GL.translate(t.target());
        glTextureImage2DEXT(buffer.handle(), GL_TEXTURE_2D, GLint(l), Format.Internal, w, h, 0, Format.External, Format.Type, t.data(0, 0, l));
        if (l == 0) buffer.set_size({ w, h });
    }
}

image_buffer<float, 1> png_to_luminance(std::vector<uint8_t> & binaryData)
{
    int width, height, nBytes;
    auto data = stbi_load_from_memory(binaryData.data(), (int)binaryData.size(), &width, &height, &nBytes, 0);
 
    image_buffer<float, 1> buffer({ width, height });

    std::cout << "Num Channels: " << nBytes << std::endl;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; ++x)
        {
            const float r = as_float<uint8_t>(data[nBytes * (y * width + x) + 0]);
            const float g = as_float<uint8_t>(data[nBytes * (y * width + x) + 1]);
            const float b = as_float<uint8_t>(data[nBytes * (y * width + x) + 2]);
            //std::cout << r << ", " << g << ", " << b << std::endl;
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

// In place
void compute_fft_2d(std::complex<float> * data, int width, int height)
{

}

//////////////////////////
//   Main Application   //
//////////////////////////

std::unique_ptr<texture_buffer> loadedTexture;
std::unique_ptr<Window> win;

int main(int argc, char * argv[])
{
    std::string loadedFilePath("No file currently loaded...");

    try
    {
        win.reset(new Window(1280, 720, "image visualizer"));
    }
    catch (const std::exception & e)
    {
        std::cout << "Caught GLFW window exception: " << e.what() << std::endl;
    }

    win->on_drop = [&](int numFiles, const char ** paths)
    {
        for (int f = 0; f < numFiles; f++)
        {
            loadedTexture.reset(new texture_buffer()); // gen handle
            const auto ext = get_extension(paths[f]);
            std::vector<uint8_t> data;

            loadedFilePath = paths[f];

            try
            {
                data = read_file_binary(std::string(paths[f]));
            }
            catch (const std::exception & e)
            {
                std::cout << "Couldn't read file: " << e.what() << std::endl;
            }

            if (ext == "png")
            {
                //upload_png(*loadedTexture.get(), data, false);

                auto img = png_to_luminance(data);
                float mean = img.compute_mean();
                std::complex<float> * imgAsComplexArray = new std::complex<float>[img.size.x * img.size.y];

                for (int y = 0; y < img.size.y; y++)
                {
                    for (int x = 0; x < img.size.x; x++)
                    {
                        imgAsComplexArray[y * img.size.x + x] = img(y, x) - mean;
                    }
                }

                compute_fft_2d(imgAsComplexArray, img.size.x, img.size.y);

                // convert back to image type

                //upload_luminance(*loadedTexture.get(), luminanceImageBuffer);

                delete imgAsComplexArray;

            }
            else if (ext == "dds")
            {
                gli::texture imgHandle(gli::load_dds((char *)data.data(), data.size()));
                upload_dds(*loadedTexture.get(), imgHandle);
            }
            else
            {
                std::cout << "Unsupported file format" << std::endl;
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
            draw_texture_buffer(0, 0, windowSize.x / 2, windowSize.y / 2, *loadedTexture.get());
        }

        draw_text(10, 16, loadedFilePath.c_str());

        glPopMatrix();

        win->swap_buffers();
    }
    return EXIT_SUCCESS;
}
