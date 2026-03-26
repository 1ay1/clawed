#include <clawed/tui/app.hpp>
#include <clawed/tui/stream_view.hpp>
#include <clawed/tui/input_view.hpp>
#include <clawed/tui/tool_view.hpp>
#include <iostream>
#include <format>
#include <thread>

namespace clawed::tui {

App::App(ApiClient& client, ToolRegistry& registry, AgentLoop::Config agent_config)
    : client_(client)
    , registry_(registry)
    , agent_(client_, registry_, std::move(agent_config))
{}

void App::run() {
    run_interactive();
}

void App::run_interactive() {
    InputView input;
    StreamView view;

    std::cout << "\033[1;36mclawed\033[0m v0.1.0 — Claude agent runtime\n";
    std::cout << "Type your message, or 'quit' to exit.\n\n";

    while (true) {
        auto line = input.read_line("\033[1;32m> \033[0m");

        if (line.empty() || line == "quit" || line == "exit") {
            std::cout << "\nBye.\n";
            break;
        }

        view.clear_current();

        // Build the UI sink that renders inline.
        UiSink ui_sink = [&](UiEvent evt) {
            std::visit(Overloaded{
                [&](UiTokens& t) {
                    std::cout << t.text << std::flush;
                    view.append_token(t.text);
                },
                [&](UiToolStart& t) {
                    std::cout << "\n"
                              << ToolView::format_tool_start(t.name, t.id)
                              << std::flush;
                    view.begin_tool_call(t.name, t.id);
                },
                [&](UiToolEnd& t) {
                    ToolCallCard card{t.id, "", t.result, t.is_error, true};
                    std::cout << ToolView::format_tool_end(card) << std::flush;
                    view.end_tool_call(t.id, t.result, t.is_error);
                },
                [&](UiStatus& s) {
                    std::cout << "\033[90m" << s.message << "\033[0m\n"
                              << std::flush;
                    view.set_status(s.message);
                },
                [&](UiError& e) {
                    std::cerr << "\033[31m" << e.error.formatted()
                              << "\033[0m\n" << std::flush;
                    view.set_error(e.error.message);
                },
                [&](UiDone&) {
                    std::cout << "\n" << std::flush;
                    view.mark_done();
                }
            }, evt);
        };

        auto result = agent_.run_turn(line, std::move(ui_sink));
        if (!result) {
            std::cerr << std::format("\033[31mError: {}\033[0m\n",
                                     result.error().formatted());
        }

        std::cout << "\n";
    }
}

} // namespace clawed::tui
