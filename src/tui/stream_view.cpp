#include <clawed/tui/stream_view.hpp>

namespace clawed::tui {

void StreamView::append_token(std::string_view text) {
    std::lock_guard lock(mu_);
    text_ += text;
}

void StreamView::begin_tool_call(std::string_view name, std::string_view id) {
    std::lock_guard lock(mu_);
    tool_cards_.push_back({
        .name      = std::string(name),
        .id        = std::string(id),
        .result    = {},
        .is_error  = false,
        .completed = false
    });
}

void StreamView::end_tool_call(std::string_view id, std::string_view result,
                               bool is_error) {
    std::lock_guard lock(mu_);
    for (auto& card : tool_cards_) {
        if (card.id == id) {
            card.result    = std::string(result);
            card.is_error  = is_error;
            card.completed = true;
            break;
        }
    }
}

void StreamView::set_status(std::string_view msg) {
    std::lock_guard lock(mu_);
    status_ = std::string(msg);
}

void StreamView::set_error(std::string_view msg) {
    std::lock_guard lock(mu_);
    error_ = std::string(msg);
}

void StreamView::mark_done() {
    std::lock_guard lock(mu_);
    done_ = true;
}

void StreamView::clear_current() {
    std::lock_guard lock(mu_);
    text_.clear();
    tool_cards_.clear();
    status_.clear();
    error_.clear();
    done_ = false;
}

auto StreamView::snapshot() -> Snapshot {
    std::lock_guard lock(mu_);
    return {text_, tool_cards_, status_, error_, done_};
}

} // namespace clawed::tui
