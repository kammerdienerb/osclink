#include <string>
#include <sstream>
#include <optional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <sys/types.h>
#include <poll.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#include <utmp.h>
#endif
#include <pwd.h>

#include "base64.hpp"

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

struct Message_Queue {

private:
    std::queue<std::string> q;
    std::mutex              mtx;
    std::condition_variable cv;

public:
    void push(const std::string &&msg) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(std::move(msg));
        cv.notify_one();
    }

    std::optional<std::string> try_pop(void) {
        std::lock_guard<std::mutex> lock(mtx);

        if (q.empty()) return {};

        auto msg = std::move(q.front());
        q.pop();

        return msg;
    }

    std::string wait_and_pop(void) {
        std::unique_lock<std::mutex> lock(mtx);

        cv.wait(lock, [this]{ return !q.empty(); });
        auto msg = q.front();
        q.pop();

        return msg;
    }
};



static int             primary_fd;
static int             replica_fd;
static struct termios  sav_term;
static Message_Queue   messages;
static const char     *osc_pattern = "\033]9998;";



static void restore_term() {
    tcsetattr(0, TCSAFLUSH, &sav_term);
    printf("\nOSCLink closed.\n");
}

static void sigwinch_handler(int sig) {
    struct winsize ws;

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    ioctl(primary_fd, TIOCSWINSZ, &ws);
}

static void sigterm_handler(int sig) {
    struct sigaction sa;

    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigemptyset (&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    restore_term();

    kill(0, SIGTERM);
}

static void setup_pty() {
    /* Set up terminal. */
    struct termios raw_term;

    tcgetattr(0, &sav_term);
    raw_term = sav_term;

    raw_term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw_term.c_oflag &= ~(OPOST | ONLCR);
    raw_term.c_cflag |= (CS8);
    raw_term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw_term.c_cc[VMIN] = 1;
    raw_term.c_cc[VTIME] = 0;

    tcsetattr(0, TCSAFLUSH, &raw_term);

    atexit(restore_term);

    /* Catch signals to change terminal size and restore terminal on exit. */
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sa.sa_handler = sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);

    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);


    /* Create a PTY and launch a shell connected to it. */
    struct winsize ws;

    ws.ws_row    = 24;
    ws.ws_col    = 80;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    openpty(&primary_fd, &replica_fd, NULL, NULL, &ws);

    const char *hello = "OSCLink activated. Run the server application locally or remotely in this terminal.\n\r";
    write(STDOUT_FILENO, hello, strlen(hello));

    sigwinch_handler(0);

    pid_t p = fork();

    if (p == 0) {
        close(primary_fd);
        login_tty(replica_fd);
        const char *shell = getenv("SHELL");
        if (shell == NULL) {
            shell = getpwuid(geteuid())->pw_shell;
        }
        if (shell == NULL) {
            shell = "/usr/bin/bash";
        }

        execlp(shell, shell, NULL);
        exit(123);
    }

    int flags = fcntl(primary_fd, F_GETFL);
    int err = fcntl(primary_fd, F_SETFL, flags | O_NONBLOCK);
    (void)err;
    flags = fcntl(STDIN_FILENO, F_GETFL);
    err = fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    close(replica_fd);
}

static int pty_read_thread_should_stop;

static void pty_read_thread() {
    std::string cur_msg;

    /* Read from PTY and look for messages. Pass STDIN on to PTY. */
    struct pollfd pfds[2];

    pfds[0].fd      = STDIN_FILENO;
    pfds[0].events  = POLLIN | POLLHUP | POLLERR;
    pfds[0].revents = 0;
    pfds[1].fd      = primary_fd;
    pfds[1].events  = POLLIN | POLLHUP | POLLERR;
    pfds[1].revents = 0;

    char buff[4096];
    int  n;
    int  w;
    int  t;

    const char *osc_state = osc_pattern;

    while (!pty_read_thread_should_stop) {
        errno = 0;

        if (poll(pfds, 2, 100) <= 0) {
            if (errno) {
                if (errno != EINTR) {
                    goto out;
                }

                errno = 0;
            }
            continue;
        }

        if (pfds[0].revents & POLLHUP || pfds[0].revents & POLLERR) { goto out; }
        if (pfds[1].revents & POLLHUP || pfds[1].revents & POLLERR) { goto out; }

        if (pfds[0].revents & POLLIN) {
            while ((n = read(pfds[0].fd, buff, sizeof(buff))) > 0) {
                t = 0;
                while (t < n) {
                    w = write(primary_fd, buff + t, n - t);

                    if (w > 0) {
                        t += w;
                    } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
                        continue;
                    } else {
                        goto out;
                    }
                }
            }

            if (n < 0) {
                if (errno != EWOULDBLOCK && errno != EINTR) {
                    goto out;
                }
            }
        }

        if (pfds[1].revents & POLLIN) {
            while ((n = read(pfds[1].fd, buff, sizeof(buff))) > 0) {
                for (int i = 0; i < n; i += 1) {
                    if (*osc_state == 0) {
                        if (buff[i] == '\x07') {
                            try {
                                messages.push(base64::from_base64(cur_msg));
                            } catch (...) {}
                            cur_msg.clear();
                            osc_state = osc_pattern;
                        } else {
                            cur_msg += buff[i];
                        }
                    } else if (buff[i] == *osc_state) {
                        osc_state += 1;
                    } else {
                        osc_state = osc_pattern;
                    }
                }

                t = 0;
                while (t < n) {
                    w = write(STDOUT_FILENO, buff + t, n - t);

                    if (w > 0) {
                        t += w;
                    } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
                        continue;
                    } else {
                        goto out;
                    }
                }
            }

            if (n < 0) {
                if (errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN) {
                    goto out;
                }
            }
        }
    }

out:;
}

static void send_to_server(std::string &&msg) {
    std::string payload = "\033]9999;";
    try {
        payload += base64::to_base64(msg);
    } catch (...) {}
    payload += "\007\n";

    int n = payload.size();
    int t = 0;
    int w = 0;
    while (t < n) {
        errno = 0;
        w = write(primary_fd, payload.c_str() + t, n - t);

        if (w > 0) {
            t += w;
        } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        } else {
            break;
        }
    }
}

// static float cast_to_float(void *data, int idx) {
//     int *ints = (int*)data;

//     return (float)ints[idx];
// }

static GLFWwindow * setup_GLFW_and_ImGui() {
    // Initialize GLFW
    if (!glfwInit()) {
        return NULL;
    }


    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100 (WebGL 1.0)
    const char* glsl_version = "#version 100";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    // GL ES 3.0 + GLSL 300 es (WebGL 2.0)
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    // Create window
    GLFWwindow* window = glfwCreateWindow(800, 600, "OSCLink", NULL, NULL);
    if (!window) {
        return NULL;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    return window;
}

struct Widget_Base {
    virtual void imgui_frame() = delete;
};

struct UI_Content_Area {
};

struct UI {
    static UI& get() {
        static UI ui;
        return ui;
    }

    UI(const UI&)            = delete;
    UI& operator=(const UI&) = delete;

    void set_window(GLFWwindow *window) {
        this->glfw_window = window;
    }

    void frame() {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();


        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ this->imgui_io.DisplaySize.x, this->imgui_io.DisplaySize.y });
        ImGui::Begin("Main", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open", "Ctrl+O")) { /* ... */ }
                if (ImGui::MenuItem("Save", "Ctrl+S")) { /* ... */ }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::Button("Get Data From Server")) {
            send_to_server("numbers please");
        }

        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(this->glfw_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(this->glfw_window);
    }

    bool window_should_close() {
        return !!glfwWindowShouldClose(this->glfw_window);
    }

private:
    ImGuiIO    &imgui_io;
    GLFWwindow *glfw_window = NULL;


    UI() : imgui_io(ImGui::GetIO()) {
        ImGui::GetStyle().WindowRounding = 0.0f;
    }

    ~UI() {
        // Cleanup
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(this->glfw_window);
        glfwTerminate();
    }
};

struct SSO_Heat_Map_Widget : Widget_Base {

#if 0
        if (heatmap.size()) {
            ImGui::BeginChild("heatmap", {}, ImGuiChildFlags_AutoResizeY);
                ImVec2 size(16, 16);
                const int heatmap_height = 10;

                ImGui::PlotLines("##", cast_to_float, heatmap.data(), heatmap.size(), 0, NULL, FLT_MAX, FLT_MAX, { size.x * (heatmap.size() / heatmap_height), 2 * size.y });

                int max = 0;
                for (int x : heatmap) {
                    if (x > max) { max = x; }
                }

                float left = ImGui::GetCursorPosX();
                float top  = ImGui::GetCursorPosY();

                int i = 0;
                for (int x : heatmap) {
                    ImGui::SetCursorPosY(top + ((i % heatmap_height) * size.y));
                    ImGui::SetCursorPosX(left + ((i / heatmap_height) * size.x));

                    ImGui::Dummy(size);

                    ImVec2 p0 = ImGui::GetItemRectMin();
                    ImVec2 p1 = ImGui::GetItemRectMax();

                    int c = 255 - (int)(((float)x / (float)max) * 255.0);
                    ImU32 col = ImGui::IsItemHovered() ? IM_COL32(255, 0, 255, 255) : IM_COL32(255, c, c, 255);

                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    draw_list->AddRectFilled(p0, p1, col);

                    i += 1;
                }
            ImGui::EndChild();
        }
#endif

};

int main() {
    if (!isatty(STDOUT_FILENO)) {
        printf("must be run in a terminal\n");
        return 1;
    }

    setup_pty();

    auto pty_thr = std::thread(pty_read_thread);

    GLFWwindow *window = setup_GLFW_and_ImGui();
    UI         &ui     = UI::get();

    ui.set_window(window);

    std::vector<std::string> new_messages;
    std::vector<int> heatmap;

    while (!ui.window_should_close()) {
//         if (auto m = messages.try_pop()) {
//             new_messages.push_back(std::move(*m));
//         }

//         if (new_messages.size()) {
//             heatmap.clear();

//             auto &data = new_messages.back();

//             std::stringstream ss(data);
//             std::string num;
//             while (std::getline(ss, num, ';')) {
//                 heatmap.push_back(std::stoi(num));
//             }

//             new_messages.clear();
//         }

        ui.frame();
    }

    pty_read_thread_should_stop = 1;
    pty_thr.join();

    return 0;

}
