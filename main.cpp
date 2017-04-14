#include <iostream>
#include <functional>
#include <map>
#include <memory>
#include <chrono>
#include <vector>
#include <stdint.h>

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

inline void draw_text(int x, int y, const char * text)
{
    char buffer[64000];
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 16, buffer);
    glDrawArrays(GL_QUADS, 0, 4 * stb_easy_font_print((float)x, (float)(y - 7), (char *)text, nullptr, buffer, sizeof(buffer)));
    glDisableClientState(GL_VERTEX_ARRAY);
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

std::unique_ptr<Window> win;

class texture_buffer
{
    GLuint tex;
public:

    texture_buffer() : tex(-1)
    {
        if (!tex) glGenTextures(1, &tex);
    }

    void upload(const gli::texture & t)
    {
        glBindTexture(GL_TEXTURE_2D, tex);

        for (std::size_t Level = 0; Level < t.levels(); ++Level)
        {
            GLsizei w = (t.extent(Level).x), h = (t.extent(Level).y);
            std::cout << w << ", " << h << std::endl;
            gli::gl GL(gli::gl::PROFILE_GL33);
            gli::gl::format const Format = GL.translate(t.format(), t.swizzles());
            GLenum Target = GL.translate(t.target());
            glTextureImage2DEXT(tex, GL_TEXTURE_2D, GLint(Level), Format.Internal, w, h, 0,Format.External, Format.Type, t.data(0, 0, Level));
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    GLuint handle() const { return tex; }
};

int main(int argc, char * argv[])
{
    try
    {
        win.reset(new Window(1280, 720, "mip visualizer"));
    }
    catch (const std::exception & e)
    {
        std::cout << "Caught GLFW window exception: " << e.what() << std::endl;
    }

    win->on_drop = [](int numFiles, const char ** paths)
    {
        for (int f = 0; f < numFiles; f++)
        {
            // todo: check extension

            std::cout << "Dropped " << paths[f] << std::endl;

            std::vector<uint8_t> data;

            try
            {
                data = read_file_binary(std::string(paths[f]));
            }
            catch (const std::exception & e)
            {
                std::cout << "Couldn't read file: " << e.what() << std::endl;
            }

            gli::texture imgHandle(gli::load_dds((char *)data.data(), data.size()));
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
        draw_text(10, 10, "Hello World");
        glPopMatrix();

        win->swap_buffers();
    }
    return EXIT_SUCCESS;
}
