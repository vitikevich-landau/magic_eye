// ОКО МАГА / eye/detail/nav/nav_session.hpp — СЕССИЯ странствия: дерево+курсор.
//   Хранит раскрытое дерево TreeItem поверх ленивых NavNode, плоскую проекцию
//   видимых строк (её рисует TreeView), курсор и операции навигации:
//   expand/collapse/пагинация. Чистая структура данных — ни терминала, ни
//   отрисовки — поэтому тестируется юнитами.
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nav_node.hpp"

namespace eye::detail::nav {

struct TreeItem {
    NavNode node;
    TreeItem* parent = nullptr;
    std::size_t depth = 0;
    bool expanded = false;
    bool loaded = false;                              // дети уже построены?
    std::vector<std::unique_ptr<TreeItem>> kids;
};

class NavSession {
public:
    explicit NavSession(std::vector<NavNode> roots) {
        for (NavNode& n : roots) {
            auto item = std::make_unique<TreeItem>();
            item->node = std::move(n);
            roots_.push_back(std::move(item));
        }
        flatten();
    }

    // --- плоская проекция для TreeView ---------------------------------------
    const std::vector<TreeItem*>& visible() const { return visible_; }
    std::size_t cursor() const { return cursor_; }
    TreeItem* current() const {
        return visible_.empty() ? nullptr : visible_[cursor_];
    }

    void move(long delta) {
        if (visible_.empty()) { cursor_ = 0; return; }
        long c = static_cast<long>(cursor_) + delta;
        if (c < 0) c = 0;
        if (c >= static_cast<long>(visible_.size()))
            c = static_cast<long>(visible_.size()) - 1;
        cursor_ = static_cast<std::size_t>(c);
    }
    void cursor_home() { cursor_ = 0; }
    void cursor_end() { cursor_ = visible_.empty() ? 0 : visible_.size() - 1; }

    // --- раскрытие / свёртка ---------------------------------------------------
    enum class Act { none, expanded, collapsed, to_parent, paged, leaf };

    // → / Enter: раскрыть узел; на «ещё…» — дозагрузить страницу на место узла.
    Act expand_current() {
        TreeItem* it = current();
        if (it == nullptr) return Act::none;
        if (it->node.kind == NodeKind::more) return page_more(it);
        if (!it->node.can_expand) return Act::leaf;
        if (!it->loaded) {
            load_children(it);
            if (it->kids.empty()) {                    // детей не оказалось
                it->node.can_expand = false;
                flatten();
                return Act::leaf;
            }
        }
        if (it->expanded) {                            // уже раскрыт → внутрь
            if (!it->kids.empty()) move(1);
            return Act::none;
        }
        it->expanded = true;
        flatten();
        return Act::expanded;
    }

    // ←: свернуть; уже свёрнут (или лист) — подняться к родителю.
    Act collapse_current() {
        TreeItem* it = current();
        if (it == nullptr) return Act::none;
        if (it->expanded) {
            it->expanded = false;
            flatten();
            return Act::collapsed;
        }
        if (it->parent != nullptr) {
            set_cursor_to(it->parent);
            return Act::to_parent;
        }
        return Act::none;
    }

    // Свернуть всё до корней (клавиша c).
    void collapse_all() {
        for (auto& r : roots_) collapse_rec(r.get());
        cursor_ = 0;
        flatten();
    }

    // Раскрыть текущий узел рекурсивно (клавиша e) — с лимитами глубины и
    // числа узлов, чтобы связный список не съел странствие целиком.
    // true — упёрлись в лимит (пусть зовущий покажет тост).
    bool expand_current_rec(std::size_t max_depth = 6,
                            std::size_t max_nodes = 500) {
        TreeItem* it = current();
        if (it == nullptr) return false;
        std::size_t budget = max_nodes;
        const bool clipped = expand_rec(it, max_depth, budget);
        flatten();
        return clipped;
    }

    // Прыжок курсора на конкретный item (виден ли — обеспечиваем раскрытием
    // родителей).
    void set_cursor_to(TreeItem* target) {
        for (TreeItem* p = target->parent; p != nullptr; p = p->parent)
            p->expanded = true;
        flatten();
        for (std::size_t i = 0; i < visible_.size(); ++i)
            if (visible_[i] == target) { cursor_ = i; return; }
    }

    // Прыжок к N-му корню (клавиши 1..9).
    void jump_to_root(std::size_t index) {
        if (index >= roots_.size()) return;
        set_cursor_to(roots_[index].get());
    }
    std::size_t root_count() const { return roots_.size(); }

    // Путь до текущего узла (breadcrumbs в шапке).
    std::string breadcrumbs() const {
        const TreeItem* it = current();
        if (it == nullptr) return "";
        std::vector<const TreeItem*> path;
        for (; it != nullptr; it = it->parent) path.push_back(it);
        std::string s;
        for (auto p = path.rbegin(); p != path.rend(); ++p) {
            if (!s.empty()) s += " ▸ ";
            s += (*p)->node.title;
        }
        return s;
    }

private:
    std::vector<std::unique_ptr<TreeItem>> roots_;
    std::vector<TreeItem*> visible_;
    std::size_t cursor_ = 0;

    void load_children(TreeItem* it) {
        it->loaded = true;
        if (!it->node.expand) return;
        for (NavNode& child : it->node.expand()) append_child(it, std::move(child));
    }

    void append_child(TreeItem* parent, NavNode child) {
        auto item = std::make_unique<TreeItem>();
        item->node = std::move(child);
        item->parent = parent;
        item->depth = parent->depth + 1;
        parent->kids.push_back(std::move(item));
    }

    // «⋯ ещё N — Enter»: заменить узел следующей страницей братьев.
    Act page_more(TreeItem* it) {
        TreeItem* parent = it->parent;
        if (parent == nullptr || !it->node.expand) return Act::none;
        std::vector<NavNode> next = it->node.expand();
        std::size_t at = 0;
        for (; at < parent->kids.size(); ++at)
            if (parent->kids[at].get() == it) break;
        if (at == parent->kids.size()) return Act::none;
        parent->kids.erase(parent->kids.begin() +
                           static_cast<std::ptrdiff_t>(at));
        for (NavNode& n : next) {
            auto item = std::make_unique<TreeItem>();
            item->node = std::move(n);
            item->parent = parent;
            item->depth = parent->depth + 1;
            parent->kids.insert(
                parent->kids.begin() + static_cast<std::ptrdiff_t>(at++),
                std::move(item));
        }
        flatten();
        if (cursor_ >= visible_.size())
            cursor_ = visible_.empty() ? 0 : visible_.size() - 1;
        return Act::paged;
    }

    void collapse_rec(TreeItem* it) {
        it->expanded = false;
        for (auto& k : it->kids) collapse_rec(k.get());
    }

    bool expand_rec(TreeItem* it, std::size_t depth_left, std::size_t& budget) {
        if (it->node.kind == NodeKind::more) return false;   // страницы — руками
        if (!it->node.can_expand || depth_left == 0) return false;
        if (budget == 0) return true;
        if (!it->loaded) load_children(it);
        it->expanded = !it->kids.empty();
        bool clipped = false;
        for (auto& k : it->kids) {
            if (budget == 0) return true;
            --budget;
            clipped |= expand_rec(k.get(), depth_left - 1, budget);
        }
        return clipped;
    }

    void flatten() {
        TreeItem* keep = visible_.empty() || cursor_ >= visible_.size()
                             ? nullptr
                             : visible_[cursor_];
        visible_.clear();
        for (auto& r : roots_) flatten_rec(r.get());
        if (keep != nullptr)
            for (std::size_t i = 0; i < visible_.size(); ++i)
                if (visible_[i] == keep) { cursor_ = i; return; }
        if (cursor_ >= visible_.size())
            cursor_ = visible_.empty() ? 0 : visible_.size() - 1;
    }
    void flatten_rec(TreeItem* it) {
        visible_.push_back(it);
        if (!it->expanded) return;
        for (auto& k : it->kids) flatten_rec(k.get());
    }
};

} // namespace eye::detail::nav
