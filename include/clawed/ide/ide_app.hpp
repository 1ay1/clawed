#pragma once

#include <clawed/ide/terminal.hpp>
#include <clawed/ide/screen.hpp>
#include <clawed/ide/widgets/code_view.hpp>
#include <clawed/ide/widgets/file_tree.hpp>
#include <clawed/ide/widgets/agent_chat.hpp>
#include <clawed/ide/widgets/status_bar.hpp>
#include <clawed/agent/loop.hpp>
#include <clawed/api/client.hpp>
#include <clawed/tool/registry.hpp>

#include <mutex>
#include <thread>
#include <vector>

namespace clawed::ide {

class IdeApp {
public:
    IdeApp(ApiClient& client, ToolRegistry& registry,
           AgentLoop::Config agent_config);

    void run();

private:
    Terminal terminal_;
    Screen screen_;
    AgentLoop agent_;

    widgets::FileTree   file_tree_;
    widgets::CodeView   code_view_;
    widgets::AgentChat  agent_chat_;
    widgets::StatusBar  status_bar_;

    enum Panel { PanelFileTree, PanelCode, PanelChat };
    Panel focused_ = PanelChat;
    bool  running_ = true;

    // Agent thread + event queue
    std::mutex queue_mu_;
    std::vector<UiEvent> pending_;
    std::jthread agent_thread_;

    void handle_event(const InputEvent& ev);
    void process_agent_updates();
    void layout_and_render();
    void submit_to_agent(std::string message);
    auto build_ui_sink() -> UiSink;
};

} // namespace clawed::ide
