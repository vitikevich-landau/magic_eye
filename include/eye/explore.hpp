// ============================================================================
//   ОКО МАГА / eye/explore.hpp — СТРАНСТВИЕ: интерактивный обозреватель
// ============================================================================
//   Полноэкранный TUI: слева дерево объектов (базы, поля, vptr, куча), справа
//   панель деталей («Гримуар»), внизу гид по клавишам. Управление — стрелки,
//   Enter/←, Tab; выход — q/Esc/Ctrl-C (терминал всегда восстанавливается).
//
//       #include <eye/magic_eye.hpp>
//       eye::explore(obj);            // одно странствие по объекту
//       eye::explore(obj, "казна");   // своя подпись корня
//
//       eye::Gallery g;               // несколько корней в одной сессии
//       g.add(knight, "рыцарь");
//       g.add(nums);
//       g.add<Config>();              // только статика типа
//       g.run();
//
//   Контракт времени жизни: объекты галереи живут, пока идёт run() —
//   Око смотрит на живую память и копий не делает (как inspect).
//
//   Деградация: не-терминал (redirect, CI) или EYE_INTERACTIVE=0 — вместо TUI
//   печатается статический inspect всех корней. EYE_SCRIPT="down enter q" —
//   исполнить клавиши и напечатать кадры (снапшот-тесты).
//
//   Слои: nav/ (ленивый граф узлов) + tui/ (терминал, канва, цикл) — оба
//   инстанцируются только при вызове explore/Gallery: программа с одним
//   inspect за странствие не платит.
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "detail/nav/nav_build.hpp"
#include "detail/tui/app.hpp"

namespace eye {

// Галерея: набор корней странствия. add() запоминает АДРЕС объекта и типовые
// замыкания; сам объект не копируется и обязан пережить run().
class Gallery {
public:
    template <class T>
    Gallery& add(const T& obj, std::string label = "") {
        roots_.push_back(detail::nav::make_object_node(
            obj, std::move(label), detail::nav::NodeKind::root));
        return *this;
    }

    // Корень «статика типа» — объект не нужен (как inspect<T>()).
    template <class T>
    Gallery& add() {
        roots_.push_back(detail::nav::make_type_node<T>());
        return *this;
    }

    // Странствие. Блокирует до выхода пользователя (q/Esc). В не-терминале
    // печатает статические панели всех корней и сразу возвращается.
    void run() {
        detail::tui::run_gallery(std::move(roots_));
        roots_.clear();
    }

    std::size_t size() const { return roots_.size(); }

private:
    std::vector<detail::nav::NavNode> roots_;
};

// Странствие по одному объекту.
template <class T>
void explore(const T& obj, const std::string& label = "") {
    Gallery g;
    g.add(obj, label);
    g.run();
}

} // namespace eye
