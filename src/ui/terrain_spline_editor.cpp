#include "ui/terrain_spline_editor.hpp"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <stdexcept>

namespace sandbox {
namespace {
void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
void number(const char* label, float& value, float lo, float hi) {
    const float old = value;
    ImGui::BeginDisabled(lo > hi);
    if (lo <= hi) {
        ImGui::DragFloat(label, &value, 0.001f, lo, hi, "%.6g",
                         ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
        value = std::isfinite(value) ? std::clamp(value, lo, hi) : old;
    }
    ImGui::EndDisabled();
}
void edit_node(TerrainSpline& node, int depth, float weirdness, float ground = 0, float smooth = 0) {
    ImGui::PushID(depth);
    const char* axes[]{"상수", "Weirdness", "PV", "Groundness", "Smoothness"};
    int axis = static_cast<int>(node.axis);
    if (ImGui::Combo("입력 축", &axis, axes, 5)) {
        const float value = std::clamp(node.evaluate(ground, smooth, weirdness), -64.0f, 64.0f);
        if (axis == 0) {
            node = {};
            node.constant = value;
        } else {
            if (node.axis == SplineAxis::constant) {
                TerrainSpline a;
                a.constant = value;
                node.locations = {-1, 1};
                node.derivatives = {0, 0};
                node.values = {a, a};
            }
            node.axis = static_cast<SplineAxis>(axis);
            node.constant = 0;
        }
    }
    help("상수 대신 Weirdness/PV 곡선을 넣을 수 있습니다. 점의 출력도 다시 곡선이 될 수 있습니다. "
         "상수로 바꾸면 현재 Weirdness 단면의 값으로 평탄화합니다.");
    if (node.axis == SplineAxis::constant) {
        number("출력값", node.constant, -64, 64);
    } else {
        int remove = -1;
        for (size_t i = 0; i < node.locations.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool expanded =
                ImGui::TreeNode("point", "점 %d · 입력 %.5g", static_cast<int>(i + 1), node.locations[i]);
            if (expanded) {
                number("입력 위치", node.locations[i], i ? node.locations[i - 1] + 0.00011f : -2,
                       i + 1 < node.locations.size() ? node.locations[i + 1] - 0.00011f : 2);
                number("접선 기울기", node.derivatives[i], -64, 64);
                help("이 점에서의 출력 변화율입니다. 0은 수평 접선이며 양수는 상승, 음수는 하강합니다. "
                     "양 끝 바깥에서는 이 기울기로 연장합니다.");
                if (depth < 8)
                    edit_node(node.values[i], depth + 1, weirdness, ground, smooth);
                else
                    number("출력값", node.values[i].constant, -64, 64);
                ImGui::BeginDisabled(node.locations.size() <= 1);
                if (ImGui::SmallButton("이 점 삭제"))
                    remove = static_cast<int>(i);
                ImGui::EndDisabled();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0) {
            node.locations.erase(node.locations.begin() + remove);
            node.derivatives.erase(node.derivatives.begin() + remove);
            node.values.erase(node.values.begin() + remove);
        }
        ImGui::BeginDisabled(node.locations.size() >= 64);
        if (ImGui::Button("가장 넓은 구간에 점 추가")) {
            if (node.locations.size() == 1) {
                const float old = node.locations.front();
                const bool prepend = old > 0;
                const float x = prepend ? -1.0f : 1.0f;
                const size_t index = prepend ? 0 : 1;
                const auto value = node.values.front();
                node.locations.insert(node.locations.begin() + index, x);
                node.derivatives.insert(node.derivatives.begin() + index, 0);
                node.values.insert(node.values.begin() + index, value);
            } else {
                size_t widest = 0;
                for (size_t i = 1; i + 1 < node.locations.size(); ++i)
                    if (node.locations[i + 1] - node.locations[i] >
                        node.locations[widest + 1] - node.locations[widest])
                        widest = i;
                if (node.locations[widest + 1] - node.locations[widest] > 0.0003f) {
                    const float x = (node.locations[widest] + node.locations[widest + 1]) * 0.5f;
                    TerrainSpline value;
                    // A new constant point samples the positive W branch for PV.
                    value.constant = std::clamp(node.evaluate(node.axis == SplineAxis::pv ? (x + 1) / 3 : x),
                                                -64.0f, 64.0f);
                    node.locations.insert(node.locations.begin() + widest + 1, x);
                    node.derivatives.insert(node.derivatives.begin() + widest + 1, 0);
                    node.values.insert(node.values.begin() + widest + 1, value);
                }
            }
        }
        help("새 점은 상수 출력과 수평 접선으로 추가하므로 주변 곡선도 달라집니다.");
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}
void edit_axis(SplineGrid& grid, bool ground, int& selected) {
    ImGui::PushID(ground ? "ground-axis" : "smooth-axis");
    auto& axis = ground ? grid.groundness : grid.smoothness;
    number(ground ? "선택 행의 Groundness" : "선택 열의 Smoothness", axis[selected],
           selected ? axis[selected - 1] + 0.00011f : -2,
           selected + 1 < static_cast<int>(axis.size()) ? axis[selected + 1] - 0.00011f : 2);
    int insert = -1;
    float coordinate = 0;
    ImGui::BeginDisabled(axis.size() >= 24);
    if (ImGui::Button(ground ? "행 추가" : "열 추가")) {
        if (axis.size() == 1) {
            insert = axis[0] > 0 ? 0 : 1;
            coordinate = insert ? 1.0f : -1.0f;
        } else {
            size_t widest = 0;
            for (size_t i = 1; i + 1 < axis.size(); ++i)
                if (axis[i + 1] - axis[i] > axis[widest + 1] - axis[widest])
                    widest = i;
            if (axis[widest + 1] - axis[widest] > 0.0003f) {
                insert = static_cast<int>(widest + 1);
                coordinate = (axis[widest] + axis[widest + 1]) * 0.5f;
            }
        }
    }
    help("가장 넓은 구간에 빈 행/열을 추가합니다. 빈 칸은 보간에 관여하지 않습니다.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(axis.size() <= 1);
    const bool remove = ImGui::Button(ground ? "선택 행 삭제" : "선택 열 삭제");
    ImGui::EndDisabled();
    if (insert >= 0 || remove) {
        const size_t old_columns = grid.smoothness.size();
        const size_t old_rows = grid.groundness.size();
        const auto old_cells = grid.cells;
        if (insert >= 0) {
            axis.insert(axis.begin() + insert, coordinate);
            selected = insert;
        } else
            axis.erase(axis.begin() + selected);
        grid.cells.assign(grid.groundness.size() * grid.smoothness.size(), std::nullopt);
        for (size_t r = 0; r < old_rows; ++r)
            for (size_t c = 0; c < old_columns; ++c) {
                const int index = static_cast<int>(ground ? r : c);
                if (remove && index == selected)
                    continue;
                const size_t target = static_cast<size_t>(index + (insert >= 0 && index >= insert ? 1 : 0) -
                                                          (remove && index > selected ? 1 : 0));
                const size_t nr = ground ? target : r, nc = ground ? c : target;
                grid.cells[nr * grid.smoothness.size() + nc] = old_cells[r * old_columns + c];
            }
        selected = std::clamp(selected, 0, static_cast<int>(axis.size()) - 1);
    }
    ImGui::PopID();
}
} // namespace
void TerrainSplineEditor::draw(TerrainSplines& splines) {
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(950, 750), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("지형 스플라인 표###terrain-splines", &open)) {
        const auto previous = splines;
        const char* outputs[]{"Offset · 높이", "Factor · 압축", "Jaggedness · 잔굴곡 강도"};
        ImGui::Combo("출력", &output_, outputs, 3);
        SplineGrid& grid = output_ == 0 ? splines.offset : output_ == 1 ? splines.factor : splines.jaggedness;
        if (grid.tree) {
            static float ground = 0, smooth = 0;
            static int axis = 0;
            number("미리보기 Groundness", ground, -2, 2);
            number("미리보기 Smoothness", smooth, -2, 2);
            number("미리보기 Weirdness", weirdness_, -2, 2);
            const char* axes[] = {"Groundness", "Smoothness", "Weirdness"};
            ImGui::Combo("단면 축", &axis, axes, 3);
            std::array<float, 257> samples{};
            for (size_t i = 0; i < samples.size(); ++i) {
                const float x = -1.2f + 2.4f * i / 256;
                samples[i] =
                    grid.evaluate(axis == 0 ? x : ground, axis == 1 ? x : smooth, axis == 2 ? x : weirdness_);
            }
            ImGui::PlotLines("##nested-curve", samples.data(), int(samples.size()), 0, axes[axis], FLT_MAX,
                             FLT_MAX, ImVec2(0, 160));
            ImGui::TextWrapped(
                "각 축의 제어점과 접선을 직접 편집합니다. 점을 펼치면 하위 곡선을 편집할 수 있습니다.");
            edit_node(*grid.tree, 0, weirdness_, ground, smooth);
        } else {
            row_ = std::clamp(row_, 0, static_cast<int>(grid.groundness.size()) - 1);
            column_ = std::clamp(column_, 0, static_cast<int>(grid.smoothness.size()) - 1);
            ImGui::TextWrapped(
                "행: Groundness · 열: Smoothness · 칸: Weirdness/PV 곡선. "
                "빈 칸은 이웃한 정의값 사이에서 보간합니다. 숫자는 Ctrl+클릭으로 직접 입력할 수 있습니다.");
            number("표에 표시할 Weirdness", weirdness_, -1, 1);
            ImGui::Text("PV: %.4f", spline_pv(weirdness_));
            if (ImGui::BeginTable("grid", static_cast<int>(grid.smoothness.size()) + 1,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, 255))) {
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableSetupColumn("Ground / Smooth", ImGuiTableColumnFlags_WidthFixed, 115);
                for (size_t c = 0; c < grid.smoothness.size(); ++c)
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 95);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted("G / S");
                for (float e : grid.smoothness) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%.5g", e);
                }
                for (size_t r = 0; r < grid.groundness.size(); ++r) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%.5g", grid.groundness[r]);
                    for (size_t c = 0; c < grid.smoothness.size(); ++c) {
                        ImGui::TableNextColumn();
                        ImGui::PushID(static_cast<int>(r * grid.smoothness.size() + c));
                        const auto& cell = grid.cells[r * grid.smoothness.size() + c];
                        char label[48];
                        if (cell)
                            std::snprintf(label, sizeof(label), "%.4g", cell->evaluate(weirdness_));
                        else
                            std::snprintf(label, sizeof(label), "—");
                        if (ImGui::Selectable(label, row_ == static_cast<int>(r) &&
                                                         column_ == static_cast<int>(c))) {
                            row_ = static_cast<int>(r);
                            column_ = static_cast<int>(c);
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            edit_axis(grid, true, row_);
            edit_axis(grid, false, column_);
            auto& cell = grid.cells[row_ * grid.smoothness.size() + column_];
            const float value = grid.evaluate(grid.groundness[row_], grid.smoothness[column_], weirdness_);
            ImGui::Text("선택 위치의 보간 출력: %.6g", value);
            if (output_ == 0)
                ImGui::Text("기준 높이: 192 + 높이 배율 × offset");
            if (output_ == 1)
                ImGui::TextWrapped("최종 압축은 factor(0.01~64) × 전체 압축 강도입니다.");
            if (output_ == 2)
                ImGui::TextWrapped("음수 강도는0으로 처리합니다. 별도 잔굴곡 노이즈의 영향량을 조절합니다.");
            ImGui::BeginDisabled(!cell);
            if (ImGui::Button("칸 복사"))
                clipboard_ = cell;
            ImGui::SameLine();
            if (ImGui::Button("칸 비우기"))
                cell.reset();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!clipboard_);
            if (ImGui::Button("붙여넣기"))
                cell = clipboard_;
            ImGui::EndDisabled();
            if (!cell && ImGui::Button("현재 보간값으로 칸 만들기")) {
                cell = TerrainSpline{};
                cell->constant = std::clamp(value, -64.0f, 64.0f);
            }
            std::array<float, 257> values{};
            for (size_t i = 0; i < values.size(); ++i)
                values[i] = grid.evaluate(grid.groundness[row_], grid.smoothness[column_],
                                          -1 + 2.0f * static_cast<float>(i) / 256);
            ImGui::PlotLines("##slice", values.data(), static_cast<int>(values.size()), 0,
                             "Weirdness -1 → +1", FLT_MAX, FLT_MAX, ImVec2(0, 130));
            if (cell)
                edit_node(*cell, 0, weirdness_);
        }
        try {
            validate(splines);
            error_.clear();
        } catch (const std::exception& e) {
            splines = previous;
            error_ = e.what();
        }
        if (!error_.empty())
            ImGui::TextWrapped("변경을 적용하지 못했습니다: %s", error_.c_str());
        ImGui::TextWrapped("실제 월드는 F8의 재생성으로, 다음 실행 기본값은 기본값 저장으로 반영합니다.");
    }
    ImGui::End();
}
} // namespace sandbox
