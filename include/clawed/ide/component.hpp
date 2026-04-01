#pragma once

#include <clawed/ide/rect.hpp>
#include <clawed/ide/screen.hpp>
#include <clawed/ide/terminal.hpp>

#include <concepts>
#include <memory>

namespace clawed::ide {

/// An IDE component renders into a cell buffer and handles input events.
template <typename T>
concept IdeComponent = requires(T& t, Rect area, Buffer& buf, const InputEvent& ev) {
    { t.render(area, buf) } -> std::same_as<void>;
    { t.on_event(ev) }      -> std::same_as<bool>;
    { t.is_dirty() }        -> std::same_as<bool>;
};

/// Type-erased component for heterogeneous storage.
class AnyComponent {
public:
    template <IdeComponent C>
    AnyComponent(C comp)
        : impl_(std::make_unique<Model<C>>(std::move(comp))) {}

    void render(Rect area, Buffer& buf)         { impl_->render(area, buf); }
    bool on_event(const InputEvent& ev)          { return impl_->on_event(ev); }
    bool is_dirty() const                        { return impl_->is_dirty(); }
    void mark_dirty()                            { impl_->mark_dirty(); }

    template <typename C>
    auto as() -> C* {
        if (auto* m = dynamic_cast<Model<C>*>(impl_.get()))
            return &m->comp;
        return nullptr;
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual void render(Rect, Buffer&) = 0;
        virtual bool on_event(const InputEvent&) = 0;
        virtual bool is_dirty() const = 0;
        virtual void mark_dirty() = 0;
    };

    template <IdeComponent C>
    struct Model final : Concept {
        C comp;
        explicit Model(C c) : comp(std::move(c)) {}
        void render(Rect a, Buffer& b) override { comp.render(a, b); }
        bool on_event(const InputEvent& e) override { return comp.on_event(e); }
        bool is_dirty() const override { return comp.is_dirty(); }
        void mark_dirty() override { comp.dirty_ = true; }
    };

    std::unique_ptr<Concept> impl_;
};

} // namespace clawed::ide
