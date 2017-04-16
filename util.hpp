#ifndef util_hpp
#define util_hpp

#include "linalg_util.hpp"
#include "gli/gli.hpp"
#include "third-party/stb/stb_image.h"
#include "third-party/stb/stb_image_write.h"
#include "third-party/stb/stb_easy_font.h"

#define GLEW_STATIC
#define GL_GLEXT_PROTOTYPES
#include "glew.h"

#define GLFW_INCLUDE_GLU
#include "GLFW\glfw3.h"

////////////////////////
//   Math Utilities   //
////////////////////////

inline float to_luminance(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

template<class T>
inline float as_float(const T & x)
{
    const float min = std::numeric_limits<T>::min();
    const float max = std::numeric_limits<T>::max();
    return (x - min) / (max - min);
}

inline bool is_power_of_two(const int & n) 
{
    return n > 0 && (n & (n - 1)) == 0;
}

template<typename T>
T clamp(const T & val, const T & min, const T & max)
{
    return std::min(std::max(val, min), max);
}

/////////////////////////
//   File Operations   //
/////////////////////////

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

///////////////////////////////////
//   Windowing & App Lifecycle   //
///////////////////////////////////

inline bool take_screenshot(int2 size)
{
    std::string timestamp = "fft";
    std::vector<uint8_t> screenShot(size.x * size.y * 3);
    glReadPixels(0, 0, size.x, size.y, GL_RGB, GL_UNSIGNED_BYTE, screenShot.data());
    auto flipped = screenShot;
    for (int y = 0; y<size.y; ++y) memcpy(flipped.data() + y*size.x * 3, screenShot.data() + (size.y - y - 1)*size.x * 3, size.x * 3);
    stbi_write_png(std::string("screenshot_" + timestamp + ".png").c_str(), size.x, size.y, 3, flipped.data(), 3 * size.x);
    return false;
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
    void set_window_size(int2 newSize) { glfwSetWindowSize(window, newSize.x, newSize.y); }
    int2 get_framebuffer_size() const { int2 size; glfwGetFramebufferSize(window, &size.x, &size.y); return size; }
    float2 get_cursor_pos() const { double2 pos; glfwGetCursorPos(window, &pos.x, &pos.y); return float2(pos); }

    void swap_buffers() { glfwSwapBuffers(window); }
    void close() { glfwSetWindowShouldClose(window, 1); }
};

#endif // end util_hpp