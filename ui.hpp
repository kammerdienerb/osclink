#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <climits>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "osclink.hpp"

namespace {

GLFWwindow * setup_GLFW_and_ImGui() {
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



struct UI_Widget_Base {
    virtual void _imgui_frame() = 0;

    void imgui_frame() {
        ImGui::PushID((int)(u64)(void*)this);
        this->_imgui_frame();
        ImGui::PopID();
    }

    virtual ~UI_Widget_Base() {}
};

struct UI_SSO_Heat_Map_Widget : UI_Widget_Base {
    static constexpr int    ROWS = 10;
    static constexpr ImVec2 SIZE = { 16, 16 };

    std::vector<float> data;
    float              max;

    void _imgui_frame() override {
        if (this->data.size()) {
            ImGui::BeginChild("heatmap", {}, ImGuiChildFlags_AutoResizeY);

                ImGui::PlotLines("##", this->data.data(), this->data.size(), 0, NULL, FLT_MAX, FLT_MAX, { SIZE.x * (this->data.size() / this->ROWS), 2 * SIZE.y });

                float left = ImGui::GetCursorPosX();
                float top  = ImGui::GetCursorPosY();

                int i = 0;
                for (float x : this->data) {
                    ImGui::SetCursorPosY(top + ((i % ROWS) * SIZE.y));
                    ImGui::SetCursorPosX(left + ((i / ROWS) * SIZE.x));

                    ImGui::Dummy(SIZE);

                    ImVec2 p0 = ImGui::GetItemRectMin();
                    ImVec2 p1 = ImGui::GetItemRectMax();

                    int c = 255 - (int)((x / this->max) * 255.0);
                    ImU32 col = ImGui::IsItemHovered() ? IM_COL32(255, 0, 255, 255) : IM_COL32(255, c, c, 255);

                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    draw_list->AddRectFilled(p0, p1, col);

                    i += 1;
                }
            ImGui::EndChild();
        }
    }

    void set_data(std::vector<float> &&data) {
        this->data = std::move(data);

        this->max = FLT_MIN;
        for (float x : this->data) {
            if (x > max) { max = x; }
        }

    }
};

struct UI_Topology_Widget : UI_Widget_Base {
    void _imgui_frame() override {
        ImGui::Text("TOPO");
    }
};

struct UI_Float_Window_Base {
    bool        show = false;
    std::string title;

    virtual void _imgui_frame() = 0;

    void imgui_frame() {
        if (this->show) {
            if (ImGui::Begin(this->title.c_str(), &this->show)) {
                this->_imgui_frame();
            }
            ImGui::End();
        }
    }

    UI_Float_Window_Base(std::string &&title) : title(title) {}

    virtual ~UI_Float_Window_Base() {}
};

struct Log_Window : UI_Float_Window_Base {
    std::vector<std::string> items;

    void _imgui_frame() override {
        for (auto &item : this->items) {
            ImGui::Text("%s", item.c_str());
        }
    }

    Log_Window() : UI_Float_Window_Base("Log") {}
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

    void set_connected(bool con) {
        this->connected = con;
    }

    void add_topology() {
        this->clear_main_panel();
        auto t = std::make_unique<UI_Topology_Widget>();
        this->main_panel_widgets.push_back(std::move(t));
    }

    void add_heatmap(std::vector<float> &&data) {
        this->clear_main_panel();
        auto h = std::make_unique<UI_SSO_Heat_Map_Widget>();
        h->set_data(std::move(data));
        this->main_panel_widgets.push_back(std::move(h));
    }

    void frame(OSCLink_Client &link) {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();


        ImGui::SetNextWindowPos({ 0, 0 });
        ImGui::SetNextWindowSize({ this->imgui_io.DisplaySize.x, this->imgui_io.DisplaySize.y });

        ImGui::Begin("Main", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Open", "Ctrl+O")) { /* ... */ }
                    if (ImGui::MenuItem("Save", "Ctrl+S")) { /* ... */ }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    if (ImGui::MenuItem("Log", "Ctrl+L")) {
                        this->get_log()->show = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            if (this->connected) {
                ImGui::BeginChild("Left", { 150, 0 }, ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
                if (ImGui::Button("Topology")) {
                    link.send("REQUEST/TOPOLOGY");
                }
                if (ImGui::Button("Heatmap")) {
                    link.send("REQUEST/HEATMAP-DATA");
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("Right", { 0, 0 }, 0);
                for (auto &widget : this->main_panel_widgets) {
                    widget->imgui_frame();
                }
                ImGui::EndChild();
            } else {
                ImGui::Text("Waiting for the server...");
            }

        ImGui::End();

        for (auto &pair : this->float_windows) {
            pair.second->imgui_frame();
        }

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

    void log(std::string &&message, bool pop_up = false) {
        this->get_log()->items.push_back(message);
        if (pop_up) {
            this->get_log()->show = true;
        }
    }

    void clear_main_panel() {
        this->main_panel_widgets.clear();
    }

private:
    ImGuiIO                                         &imgui_io;
    GLFWwindow                                      *glfw_window = NULL;
    std::map<std::string,
             std::unique_ptr<UI_Float_Window_Base>>  float_windows;
    std::vector<std::unique_ptr<UI_Widget_Base>>     main_panel_widgets;
    bool                                             connected = false;

    UI() : imgui_io(ImGui::GetIO()) {
        ImGui::GetStyle().WindowRounding = 0.0f;

        this->float_windows["Log"] = std::make_unique<Log_Window>();
    }

    ~UI() {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(this->glfw_window);
        glfwTerminate();
    }

    Log_Window *get_log() {
        return dynamic_cast<Log_Window*>(this->float_windows["Log"].get());
    }
};

}
