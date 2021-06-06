// Copyright (c) 2020 INRA Distributed under the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include "application.hpp"
#include "dialog.hpp"
#include "node-editor.hpp"

#include <irritator/core.hpp>

#include <chrono>

namespace irt {

bool application::init()
{
    if (auto ret = editors.init(50u); is_bad(ret)) {
        log_w.log(2, "Fail to initialize irritator: %s\n", status_string(ret));
        std::fprintf(stderr, "Fail to initialize irritator: %s\n", status_string(ret));
        return false;
    }

    if (auto* ed = app.alloc_editor(); ed) {
        ed->context = imnodes::EditorContextCreate();
        ed->settings.compute_colors();
    }

    try {
        if (auto home = get_home_directory(); home) {
            home_dir = home.value();
            home_dir /= "irritator";
        } else {
            log_w.log(
              3,
              "Fail to retrieve home directory. Use current directory instead");
            home_dir = std::filesystem::current_path();
        }

        if (auto install = get_executable_directory(); install) {
            executable_dir = install.value();
        } else {
            log_w.log(
              3,
              "Fail to retrieve executable directory. Use current directory "
              "instead");
            executable_dir = std::filesystem::current_path();
        }

        log_w.log(5,
                  "home: %s\ninstall: %s\n",
                  home_dir.u8string().c_str(),
                  executable_dir.u8string().c_str());
        return true;
    } catch (const std::exception& /*e*/) {
        log_w.log(2, "Fail to initialize application");
        return false;
    }
}

bool
void application::show()
{
    bool ret = true;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) {
                if (auto* ed = alloc_editor(); ed)
                    ed->context = imnodes::EditorContextCreate();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
                ret = false;

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")) {
            editor* ed = nullptr;
            while (editors.next(ed))
                ImGui::MenuItem(ed->name.c_str(), nullptr, &ed->show);

            ImGui::MenuItem("Simulation", nullptr, &show_simulation);
            ImGui::MenuItem("Plot", nullptr, &show_plot);
            ImGui::MenuItem("Sources", nullptr, &show_sources_window);
            ImGui::MenuItem("Settings", nullptr, &show_settings);
            ImGui::MenuItem("Log", nullptr, &show_log);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Demo window", nullptr, &show_demo);

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    editor* ed = nullptr;
    while (editors.next(ed)) {
        if (ed->show) {
            if (!ed->show_editor()) {
                editor* next = ed;
                editors.next(next);
                free_editor(*ed);
            } else {
                if (ed->show_settings)
                    ed->settings.show(&ed->show_settings);
            }
        }
    }

    if (show_simulation)
        show_simulation_window();

    if (show_plot)
        show_plot_window();

    if (show_log)
        log_w.show(&show_log);

    if (show_settings)
        settings.show(&show_settings);

    if (show_demo)
        ImGui::ShowDemoWindow();

    if (show_sources_window)
        show_sources(&show_sources_window);

    return ret;
}


editor*
application::alloc_editor()
{
    if (srcs.binary_file_sources.capacity() == 0) {
        srcs.init(50);
    }

    if (!editors.can_alloc(1u)) {
        log_w.log(2, "Too many open editor\n");
        return nullptr;
    }

    auto& ed = editors.alloc();
    if (auto ret = ed.initialize(get_index(editors.get_id(ed))); is_bad(ret)) {
        log_w.log(2, "Fail to initialize irritator: %s\n", status_string(ret));
        editors.free(ed);
        return nullptr;
    }

    log_w.log(5, "Open editor %s\n", ed.name.c_str());
    return &ed;
}

void
application::free_editor(editor& ed)
{
    log_w.log(5, "Close editor %s\n", ed.name.c_str());
    editors.free(ed);
}

void
application::show_plot_window()
{
    ImGui::SetNextWindowPos(ImVec2(50, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_Once);
    if (!ImGui::Begin("Plot", &show_plot)) {
        ImGui::End();
        return;
    }

    static editor_id current = undefined<editor_id>();
    if (auto* ed = make_combo_editor_name(*this, current); ed) {
        if (ImPlot::BeginPlot("simulation", "t", "s")) {
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.f);

            plot_output* out = nullptr;
            while (ed->plot_outs.next(out)) {
                if (!out->xs.empty() && !out->ys.empty())
                    ImPlot::PlotLine(out->name.c_str(),
                                     out->xs.data(),
                                     out->ys.data(),
                                     static_cast<int>(out->xs.size()));
            }

            ImPlot::PopStyleVar(1);
            ImPlot::EndPlot();
        }
    }

    ImGui::End();
}


void application::show_settings_window()
{
    ImGui::SetNextWindowPos(ImVec2(300, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_Once);
    if (!ImGui::Begin("Settings", &show_settings)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Home.......: %s", home_dir.u8string().c_str());
    ImGui::Text("Executable.: %s", executable_dir.u8string().c_str());

    ImGui::End();   
}

void
application::shutdown()
{
    editor* ed = nullptr;
    while (editors.next(ed))
        imnodes::EditorContextFree(ed->context);
}

static status
simulation_run_for(simulation& sim,
                   long long int duration_in_microseconds,
                   const double end,
                   double& current) noexcept
{
    namespace stdc = std::chrono;

    auto start_at = stdc::high_resolution_clock::now();
    long long int duration_since_start;

    do {
        if (sim.current_status = sim.run(current); is_bad(sim.current_status))
           return ret;

        auto end_at = stdc::high_resolution_clock::now();
        auto duration = end_at - start_at;
        auto duration_cast = stdc::duration_cast<stdc::microseconds>(duration);
        duration_since_start = duration_cast.count();
    } while (duration_since_start < duration_in_microseconds);

    return status::success;
}

void application::run_simulations()
{
    int running_simulation = 0;

    editor* ed = nullptr;
    while (editors.next(ed))
        if (ed->st = editor_status::running)
            ++running_simulation;

    if (!running_simulation)
        return;

    auto duration = 10000 / running_simulation;
    
    ed = nullptr;
    while (editors.next(ed))
        if (ed->st = editor_status::running)
            ed->sim_st = run_for(ed->sim, duration, ed->simulation_end, ed->simulation_current);
}

} // namespace irt
