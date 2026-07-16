// ОКО МАГА / eye/detail/nav/nav_session.hpp — СЕССИЯ странствия: дерево+курсор.
//   Хранит раскрытое дерево TreeItem поверх ленивых NavNode, плоскую проекцию
//   видимых строк (её рисует TreeView), курсор и операции навигации:
//   expand/collapse/пагинация. Чистая структура данных — ни терминала, ни
//   отрисовки — поэтому тестируется юнитами.
#pragma once
#include <cstddef>
#include <map>
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
    TreeItem* shared_of = nullptr;   // общая virtual-база: оригинал в другой ветке
};

class NavSession {
public:
    explicit NavSession(std::vector<NavNode> roots) {
        for (NavNode& n : roots) {
            auto item = std::make_unique<TreeItem>();
            item->node = std::move(n);
            register_item(item.get());
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
    void set_cursor_index(std::size_t i) {
        if (i < visible_.size()) cursor_ = i;
    }

    // --- раскрытие / свёртка ---------------------------------------------------
    enum class Act { none, expanded, collapsed, to_parent, paged, leaf };

    // → / Enter: раскрыть узел; на «ещё…» — дозагрузить страницу на место узла.
    Act expand_current() {
        TreeItem* it = current();
        if (it == nullptr) return Act::none;
        if (it->shared_of != nullptr) {   // общая virtual-база → прыжок к оригиналу
            set_cursor_to(it->shared_of);
            return Act::to_parent;
        }
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

    // --- переход по указателю (M-D) --------------------------------------------
    // Итог перехода: moved — pointee прикреплён ребёнком и курсор на нём;
    // jumped — цель уже была в дереве (цикл ⟲) — прыжок без дубля;
    // blocked — тип/nullptr не пускают (reason — тост для гида).
    enum class Follow { none, moved, jumped, blocked };
    struct FollowOutcome {
        Follow what = Follow::none;
        std::string reason;
    };

    FollowOutcome follow_current() {
        TreeItem* it = current();
        if (it == nullptr) return {};
        NavNode& n = it->node;
        if (!n.can_follow) {
            if (!n.follow_block.empty())
                return {Follow::blocked, n.follow_block};
            return {};
        }
        NavNode target = n.follow();
        // Цикл: (адрес, тип) уже материализован — прыжок, а не второй узел.
        // Так связный список обходится честно: каждый узел строится один раз.
        const MatKey key{target.addr, target.type};
        if (const auto hit = materialized_.find(key);
            hit != materialized_.end() && hit->second != it) {
            history_.push_back(it);
            set_cursor_to(hit->second);
            return {Follow::jumped, ""};
        }
        history_.push_back(it);
        append_child(it, std::move(target));
        it->expanded = true;
        TreeItem* child = it->kids.back().get();
        flatten();
        set_cursor_to(child);
        return {Follow::moved, ""};
    }

    // ⌫ — назад по истории переходов. false — истории нет.
    bool back() {
        if (history_.empty()) return false;
        TreeItem* to = history_.back();
        history_.pop_back();
        set_cursor_to(to);
        return true;
    }
    std::size_t history_depth() const { return history_.size(); }

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
    // Ключ материализации: адрес + имя типа. Один и тот же адрес живёт под
    // разными типами (объект и его primary-база) — это РАЗНЫЕ узлы.
    using MatKey = std::pair<const void*, std::string>;

    std::vector<std::unique_ptr<TreeItem>> roots_;
    std::vector<TreeItem*> visible_;
    std::size_t cursor_ = 0;
    std::vector<TreeItem*> history_;               // след переходов (⌫)
    std::map<MatKey, TreeItem*> materialized_;     // (адрес, тип) → узел

    // В реестр циклов идут ТОЛЬКО узлы-объекты (корень/pointee/под-объект базы)
    // — те, к которым осмысленно «вернуться» по указателю. Лист-поле (даже
    // структурного типа) — не объект: указатель на такое поле обязан строить
    // разворачиваемый *p, а не ложно ловиться как цикл и прыгать на
    // нераскрываемый узел поля (ревью Codex, PR #5).
    static bool is_object_kind(NodeKind k) {
        return k == NodeKind::root || k == NodeKind::object ||
               k == NodeKind::base;
    }
    // Зарегистрировать узел-объект и вернуть ВЛАДЕЛЬЦА ключа (адрес, тип):
    // либо только что вставленный it, либо ранее занявший этот ключ. Для
    // нетрекаемых узлов (не-объект/без адреса) владелец — сам it.
    TreeItem* register_item(TreeItem* it) {
        const NavNode& n = it->node;
        if (n.addr == nullptr || n.type.empty() || !is_object_kind(n.kind))
            return it;
        return materialized_.emplace(MatKey{n.addr, n.type}, it).first->second;
    }

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
        TreeItem* owner = register_item(item.get());
        // Тот же под-объект базы УЖЕ есть в дереве (общая virtual-база ромба:
        // обе ветки ведут к одному Being). Не плодим второй разворачиваемый
        // узел — метим общим и не разворачиваем; Enter прыгнет к оригиналу.
        // Так дерево совпадает с картой памяти, где второй помечен «общий».
        // (Повторная НЕвиртуальная база — РАЗНЫЕ адреса, ключ не совпадёт.)
        if (item->node.kind == NodeKind::base && owner != item.get()) {
            item->shared_of = owner;
            item->node.can_expand = false;
            item->node.expand = nullptr;
            item->node.preview += item->node.preview.empty()
                                      ? "общий (virtual, см. выше)"
                                      : " · общий (см. выше)";
        }
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
            register_item(item.get());
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
