#pragma once

// ── Streaming Markdown → ANSI Renderer ──────────────────────────────────────
//
// ZERO LATENCY for text. Tables buffer until complete for column alignment.
//
// Hot path: tokens → memcpy into Terminal buffer → one write() syscall.
// Tables: accumulate rows → calculate column widths → render aligned.

#include <clawed/tui/ansi.hpp>
#include <clawed/tui/terminal.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace clawed::tui {

class MdRenderer {
public:
    void reset() {
        line_buf_.clear();
        in_code_block_ = false;
        in_table_ = false;
        first_output_ = true;
        had_tools_ = false;
        line_has_output_ = false;
        table_rows_.clear();
    }

    void feed(std::string_view text, Terminal& term) __attribute__((hot)) {
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                auto span = text.substr(start, i - start);
                if (!span.empty()) {
                    if (!in_table_) {
                        if (!line_has_output_) { emit_prefix(term); line_has_output_ = true; }
                        term.write(span);
                    }
                    line_buf_.append(span.data(), span.size());
                }
                finish_line(term);
                line_buf_.clear();
                line_has_output_ = false;
                start = i + 1;
            }
        }
        auto remaining = text.substr(start);
        if (!remaining.empty()) {
            // If in table mode OR first char of a new line is |, buffer silently
            bool suppress = in_table_;
            if (!suppress && line_buf_.empty() && !line_has_output_ && remaining[0] == '|')
                suppress = true;

            if (suppress) {
                line_buf_.append(remaining.data(), remaining.size());
            } else {
                if (!line_has_output_) { emit_prefix(term); line_has_output_ = true; }
                term.write(remaining);
                line_buf_.append(remaining.data(), remaining.size());
            }
        }
        term.flush();
    }

    void flush(Terminal& term) {
        flush_table(term);
        if (!line_buf_.empty() && line_has_output_) {
            term.clear_line();
            using namespace ansi;
            if (in_code_block_) {
                term.write("  "); term.write(GR);
                term.write("\xe2\x94\x82"); term.write(RST);
                term.write(" "); term.write(line_buf_);
            } else {
                term.write("  ");
                term.write(render_inline(line_buf_));
            }
            term.flush();
            line_buf_.clear();
            line_has_output_ = false;
        }
    }

    void set_had_tools(bool v) { had_tools_ = v; }
    [[nodiscard]] auto has_partial() const -> bool { return line_has_output_; }

private:
    std::string line_buf_;
    bool in_code_block_ = false;
    bool in_table_ = false;
    bool first_output_ = true;
    bool had_tools_ = false;
    bool line_has_output_ = false;

    // Table buffering for column alignment
    std::vector<std::string> table_rows_;

    void emit_prefix(Terminal& term) {
        if (first_output_) {
            if (had_tools_) term.write("\n");
            first_output_ = false;
        }
        if (in_code_block_) {
            using namespace ansi;
            term.write("  "); term.write(GR);
            term.write("\xe2\x94\x82"); term.write(RST); term.write(" ");
        } else {
            term.write("  ");
        }
    }

    // ── Table rendering with column alignment ───────────────────────────────

    static auto split_cells(const std::string& row) -> std::vector<std::string> {
        std::vector<std::string> cells;
        size_t i = 0;
        // Skip leading |
        if (i < row.size() && row[i] == '|') ++i;
        while (i < row.size()) {
            size_t start = i;
            while (i < row.size() && row[i] != '|') ++i;
            auto cell = row.substr(start, i - start);
            // Trim whitespace
            size_t a = cell.find_first_not_of(' ');
            size_t b = cell.find_last_not_of(' ');
            if (a != std::string::npos)
                cells.push_back(cell.substr(a, b - a + 1));
            else
                cells.push_back("");
            if (i < row.size()) ++i; // skip |
        }
        return cells;
    }

    static auto is_separator_row(const std::string& row) -> bool {
        for (char c : row)
            if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
        return row.find("---") != std::string::npos;
    }

    // Approximate visible width (handle multi-byte UTF-8 and wide chars)
    static auto visible_width(const std::string& s) -> size_t {
        size_t w = 0;
        size_t i = 0;
        while (i < s.size()) {
            auto c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) { w++; i++; }
            else if (c < 0xC0) { i++; }              // continuation byte
            else if (c < 0xE0) { w++; i += 2; }      // 2-byte
            else if (c < 0xF0) {
                // 3-byte — check for wide chars (CJK, emoji indicators)
                // Most emoji are 4-byte, 3-byte are typically 1-width
                w++; i += 3;
            }
            else { w += 2; i += 4; }                  // 4-byte (emoji = 2 width)
        }
        return w;
    }

    void flush_table(Terminal& term) {
        if (table_rows_.empty()) return;
        using namespace ansi;

        emit_first_if_needed(term);

        // Parse all rows into cells
        std::vector<std::vector<std::string>> parsed;
        std::vector<bool> is_sep;
        size_t max_cols = 0;

        for (auto& row : table_rows_) {
            if (is_separator_row(row)) {
                parsed.push_back({});
                is_sep.push_back(true);
            } else {
                auto cells = split_cells(row);
                max_cols = std::max(max_cols, cells.size());
                parsed.push_back(std::move(cells));
                is_sep.push_back(false);
            }
        }

        // Calculate column widths
        std::vector<size_t> col_widths(max_cols, 0);
        for (size_t r = 0; r < parsed.size(); ++r) {
            if (is_sep[r]) continue;
            for (size_t c = 0; c < parsed[r].size(); ++c)
                col_widths[c] = std::max(col_widths[c], visible_width(parsed[r][c]));
        }

        // Minimum width of 3 for each column
        for (auto& w : col_widths) w = std::max(w, size_t{3});

        // Render each row
        for (size_t r = 0; r < parsed.size(); ++r) {
            term.write("  ");

            if (is_sep[r]) {
                // Separator: ├───┼───┼───┤ or ┼───┼───┼
                term.write(GR);
                for (size_t c = 0; c < max_cols; ++c) {
                    if (c == 0) term.write("\xe2\x94\x9c");  // ├
                    else term.write("\xe2\x94\xbc");          // ┼
                    for (size_t j = 0; j < col_widths[c] + 2; ++j)
                        term.write("\xe2\x94\x80");           // ─
                }
                term.write("\xe2\x94\xa4");                   // ┤
                term.write(RST);
            } else {
                // Data row: │ cell │ cell │
                auto& cells = parsed[r];
                for (size_t c = 0; c < max_cols; ++c) {
                    term.write(GR); term.write("\xe2\x94\x82"); term.write(RST);
                    term.write(" ");
                    std::string cell = c < cells.size() ? cells[c] : "";
                    term.write(cell);
                    // Pad to column width
                    auto vw = visible_width(cell);
                    auto pad = col_widths[c] > vw ? col_widths[c] - vw : 0;
                    for (size_t j = 0; j < pad + 1; ++j) term.write(" ");
                }
                term.write(GR); term.write("\xe2\x94\x82"); term.write(RST);
            }
            term.write("\n");
        }

        table_rows_.clear();
    }

    void emit_first_if_needed(Terminal& term) {
        if (first_output_) {
            if (had_tools_) term.write("\n");
            first_output_ = false;
        }
    }

    // ── Line completion ─────────────────────────────────────────────────────

    void finish_line(Terminal& term) {
        using namespace ansi;

        if (!line_has_output_) {
            flush_table(term);
            emit_first_if_needed(term);
            term.write("\n");
            return;
        }

        term.clear_line();
        auto& L = line_buf_;

        // Check if this is a table row — buffer silently for alignment
        if (!L.empty() && L[0] == '|' && !in_code_block_) {
            table_rows_.push_back(L);
            in_table_ = true;
            return;
        }

        // Not a table row — flush any buffered table, exit table mode
        if (in_table_) {
            flush_table(term);
            in_table_ = false;
        }

        // Code fence
        if (L.starts_with("```")) {
            if (!in_code_block_) {
                in_code_block_ = true;
                auto lang = L.substr(3);
                term.write("  "); term.write(GR);
                term.write("\xe2\x94\x8c ");
                if (!lang.empty()) term.write(lang);
                else term.write("\xe2\x94\x80\xe2\x94\x80");
                term.write(RST); term.write("\n");
            } else {
                in_code_block_ = false;
                term.write("  "); term.write(GR);
                term.write("\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80");
                term.write(RST); term.write("\n");
            }
            return;
        }

        // Code block content
        if (in_code_block_) {
            term.write("  "); term.write(GR);
            term.write("\xe2\x94\x82"); term.write(RST);
            term.write(" "); term.write(L); term.write("\n");
            return;
        }

        // Headers
        if (!L.empty() && L[0] == '#') {
            size_t lvl = 0;
            while (lvl < L.size() && L[lvl] == '#') ++lvl;
            if (lvl <= 4 && lvl < L.size() && L[lvl] == ' ') {
                auto body = render_inline(L.substr(lvl + 1));
                term.write("  ");
                if (lvl == 1) { term.write(BOLD); term.write(C); }
                else if (lvl == 2) { term.write(BOLD); term.write(W); }
                else { term.write(BOLD); }
                term.write(body); term.write(RST); term.write("\n");
                return;
            }
        }

        // HR
        if ((L.starts_with("---") || L.starts_with("***")) &&
            L.find_first_not_of("-* ") == std::string::npos) {
            auto w = std::max(10, static_cast<int>(term.width()) - 6);
            term.write("  "); term.write(GR);
            for (int i = 0; i < w; ++i) term.write('-');
            term.write(RST); term.write("\n");
            return;
        }

        // Bullet list
        if (L.size() > 2 && (L[0] == '-' || L[0] == '*') && L[1] == ' ') {
            term.write("  "); term.write(GR);
            term.write("\xe2\x80\xa2"); term.write(RST);
            term.write(" "); term.write(render_inline(L.substr(2))); term.write("\n");
            return;
        }

        // Numbered list
        if (L.size() > 2 && L[0] >= '1' && L[0] <= '9') {
            auto dot = L.find(". ");
            if (dot != std::string::npos && dot < 4) {
                term.write("  "); term.write(GR);
                term.write(L.substr(0, dot + 1));
                term.write(RST); term.write(" ");
                term.write(render_inline(L.substr(dot + 2)));
                term.write("\n");
                return;
            }
        }

        // Indented code
        if (L.starts_with("    ") || (!L.empty() && L[0] == '\t')) {
            term.write("  "); term.write(GL);
            term.write(L); term.write(RST); term.write("\n");
            return;
        }

        // Regular paragraph
        term.write("  "); term.write(render_inline(L)); term.write("\n");
    }

    // ── Inline markdown → ANSI ──────────────────────────────────────────────

    static auto render_inline(const std::string& text) -> std::string {
        using namespace ansi;
        std::string out;
        out.reserve(text.size() + 64);
        size_t i = 0;
        while (i < text.size()) {
            if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
                auto end = text.find("**", i + 2);
                if (end != std::string::npos) {
                    out += BOLD; out.append(text, i + 2, end - i - 2); out += RST;
                    i = end + 2; continue;
                }
            }
            if (text[i] == '*' && (i + 1 >= text.size() || text[i + 1] != '*')) {
                auto end = text.find('*', i + 1);
                if (end != std::string::npos && end > i + 1) {
                    out += ITAL; out.append(text, i + 1, end - i - 1); out += RST;
                    i = end + 1; continue;
                }
            }
            if (text[i] == '`') {
                auto end = text.find('`', i + 1);
                if (end != std::string::npos) {
                    out += C; out.append(text, i + 1, end - i - 1); out += RST;
                    i = end + 1; continue;
                }
            }
            if (text[i] == '[') {
                auto bracket = text.find(']', i + 1);
                if (bracket != std::string::npos &&
                    bracket + 1 < text.size() && text[bracket + 1] == '(') {
                    auto paren = text.find(')', bracket + 2);
                    if (paren != std::string::npos) {
                        out += ULINE; out.append(text, i + 1, bracket - i - 1); out += RST;
                        i = paren + 1; continue;
                    }
                }
            }
            out += text[i]; ++i;
        }
        return out;
    }
};

} // namespace clawed::tui
