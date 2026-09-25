#include <windows.h>
#include <cstdint>

#include "plugin2.h"
#include "planner.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kMaximumLayer = 10000;
constexpr wchar_t kTitle[] = L"LayerPack";
constexpr wchar_t kScopeItem[] = L"対象レイヤー数";
EDIT_HANDLE* g_edit = nullptr;
COMMON_PLUGIN_TABLE g_plugin{L"LayerPack", L"空レイヤーを圧縮し、制御オブジェクトの対象範囲を維持します"};

struct Object {
  OBJECT_HANDLE handle{};
  OBJECT_LAYER_FRAME location{};
};

struct Control {
  size_t object_index{};
  std::wstring effect;
  int original_count{};
};

struct Snapshot {
  int scene_id = -1;
  std::vector<layer_pack::Layer> layers;
  std::vector<Object> objects;
  std::vector<Control> controls;
  layer_pack::Plan plan;
  std::wstring error;
};

bool parse_count(std::string_view value, int& result) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
  if (value.empty()) return false;
  int parsed = -1;
  auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size() || parsed < 0) return false;
  result = parsed;
  return true;
}

void add_control_if_present(EDIT_SECTION* edit, Snapshot& out, size_t object_index,
                            const wchar_t* effect) {
  if (!out.error.empty()) return;
  LPCSTR raw = edit->get_object_item_value(out.objects[object_index].handle, effect, kScopeItem);
  if (!raw) return;
  int count = -1;
  if (!parse_count(raw, count)) {
    out.error = L"対象レイヤー数を整数として読めない制御オブジェクトがあります。変更しません。";
    return;
  }
  out.controls.push_back({object_index, effect, count});
}

void collect(EDIT_SECTION* edit, Snapshot& out) {
  EDIT_INFO info{};
  g_edit->get_edit_info(&info, sizeof(info));
  out.scene_id = info.scene_id;
  if (info.layer_max < 0) return;
  if (info.layer_max > kMaximumLayer) {
    out.error = L"レイヤー数が安全上限を超えています。変更しません。";
    return;
  }
  const int count = info.layer_max + 1;
  out.layers.resize(count);
  for (int layer = 0; layer < count; ++layer) {
    LPCWSTR name = edit->get_layer_name(layer);
    const bool named = name && *name;
    const bool protected_layer = named || !edit->get_layer_enable(layer) || edit->get_layer_lock(layer);
    out.layers[layer].protected_layer = protected_layer;
    int next_frame = 0;
    while (OBJECT_HANDLE handle = edit->find_object(layer, next_frame)) {
      auto position = edit->get_object_layer_frame(handle);
      if (position.layer != layer || position.end < next_frame || position.end == std::numeric_limits<int>::max()) {
        out.error = L"オブジェクトの位置を安全に列挙できません。変更しません。";
        return;
      }
      out.layers[layer].occupied = true;
      out.objects.push_back({handle, position});
      const size_t index = out.objects.size() - 1;

      // The two control types requested by the user have a finite or unlimited
      // lower-layer scope. Other range-bearing object types are not silently
      // compressed until their semantics are explicitly supported.
      add_control_if_present(edit, out, index, L"グループ制御");
      add_control_if_present(edit, out, index, L"カメラ制御");
      if (!out.error.empty()) return;
      LPCSTR alias_ptr = edit->get_object_alias(handle);
      if (alias_ptr) {
        std::string alias(alias_ptr);
        if (alias.find("対象レイヤー数=") != std::string::npos) {
          bool known = false;
          for (const auto& control : out.controls)
            if (control.object_index == index) known = true;
          if (!known) {
            out.error = L"未対応のレイヤー範囲指定オブジェクトがあります。安全のため変更しません。";
            return;
          }
        }
      }
      next_frame = position.end + 1;
    }
  }
  std::vector<layer_pack::Scope> scopes;
  scopes.reserve(out.controls.size());
  for (const auto& control : out.controls)
    scopes.push_back({out.objects[control.object_index].location.layer, control.original_count});
  try {
    out.plan = layer_pack::make_plan(out.layers, scopes);
  } catch (...) {
    out.error = L"レイヤー圧縮の計画を作れません。変更しません。";
  }
}

bool same_plan(const Snapshot& a, const Snapshot& b) {
  if (a.scene_id != b.scene_id || a.objects.size() != b.objects.size() ||
      a.controls.size() != b.controls.size() || a.plan.destination != b.plan.destination ||
      a.plan.scope_count != b.plan.scope_count) return false;
  for (size_t i = 0; i < a.objects.size(); ++i) {
    const auto x = a.objects[i].location, y = b.objects[i].location;
    if (x.layer != y.layer || x.start != y.start || x.end != y.end) return false;
  }
  for (size_t i = 0; i < a.controls.size(); ++i)
    if (a.controls[i].original_count != b.controls[i].original_count ||
        a.controls[i].effect != b.controls[i].effect ||
        a.controls[i].object_index != b.controls[i].object_index) return false;
  return true;
}

struct ApplyRequest {
  const Snapshot* preview{};
  std::wstring error;
  int moved_objects = 0;
  int changed_scopes = 0;
};

void apply(void* raw, EDIT_SECTION* edit) {
  auto& request = *static_cast<ApplyRequest*>(raw);
  Snapshot fresh;
  collect(edit, fresh);
  if (!fresh.error.empty()) { request.error = fresh.error; return; }
  if (!same_plan(*request.preview, fresh)) {
    request.error = L"確認中にシーンが変わりました。もう一度実行してください。";
    return;
  }
  std::vector<size_t> moved;
  std::vector<size_t> changed;
  moved.reserve(fresh.objects.size());
  changed.reserve(fresh.controls.size());
  for (size_t i = 0; i < fresh.objects.size(); ++i) {
    const auto& object = fresh.objects[i];
    const int target = fresh.plan.destination[object.location.layer];
    if (target == object.location.layer) continue;
    if (target < 0 || !edit->move_object(object.handle, target, object.location.start)) {
      request.error = L"オブジェクトの移動に失敗しました。元に戻せる範囲で復旧します。";
      break;
    }
    moved.push_back(i);
  }
  if (request.error.empty()) {
    for (size_t i = 0; i < fresh.controls.size(); ++i) {
      const auto& control = fresh.controls[i];
      const int updated = fresh.plan.scope_count[i];
      if (updated == control.original_count) continue;
      if (!edit->set_object_item_value(fresh.objects[control.object_index].handle,
                                       control.effect.c_str(), kScopeItem,
                                       std::to_string(updated).c_str())) {
        request.error = L"制御オブジェクトの対象範囲を更新できません。復旧を試みます。";
        break;
      }
      changed.push_back(i);
    }
  }
  if (!request.error.empty()) {
    bool recovered = true;
    for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
      const auto& control = fresh.controls[*it];
      recovered &= edit->set_object_item_value(fresh.objects[control.object_index].handle,
                                               control.effect.c_str(), kScopeItem,
                                               std::to_string(control.original_count).c_str());
    }
    for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
      const auto& object = fresh.objects[*it];
      recovered &= edit->move_object(object.handle, object.location.layer, object.location.start);
    }
    if (!recovered) request.error += L" Ctrl+Zで操作全体を戻してください。";
    return;
  }
  request.moved_objects = static_cast<int>(moved.size());
  request.changed_scopes = static_cast<int>(changed.size());
  if (request.moved_objects || request.changed_scopes) edit->set_edited_state();
}

void on_menu(void*) {
  if (!g_edit) return;
  Snapshot preview;
  if (!g_edit->call_read_section_param(&preview, [](void* raw, EDIT_SECTION* edit) {
        collect(edit, *static_cast<Snapshot*>(raw));
      })) return;
  HWND parent = g_edit->get_host_app_window();
  if (!preview.error.empty()) {
    MessageBoxW(parent, preview.error.c_str(), kTitle, MB_OK | MB_ICONWARNING);
    return;
  }
  int changes = 0;
  for (size_t i = 0; i < preview.controls.size(); ++i)
    changes += preview.controls[i].original_count != preview.plan.scope_count[i];
  if (preview.plan.moved_layers == 0 && changes == 0) {
    MessageBoxW(parent, L"圧縮できる空レイヤーはありません。", kTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  const std::wstring question = L"現在のシーンを圧縮しますか？\n\n移動するレイヤー: " +
      std::to_wstring(preview.plan.moved_layers) + L"\n調整する制御範囲: " +
      std::to_wstring(changes) + L"\n範囲保護のため残す空レイヤー: " +
      std::to_wstring(preview.plan.retained_guards) +
      L"\n\n名前付き・非表示・ロック中のレイヤーは固定します。";
  if (MessageBoxW(parent, question.c_str(), kTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  ApplyRequest request{&preview};
  if (!g_edit->call_edit_section_param(&request, apply)) {
    MessageBoxW(parent, L"現在は編集できません。再生や出力が終わってから試してください。", kTitle,
                MB_OK | MB_ICONWARNING);
    return;
  }
  if (!request.error.empty())
    MessageBoxW(parent, request.error.c_str(), kTitle, MB_OK | MB_ICONWARNING);
}

} // namespace

extern "C" __declspec(dllexport) DWORD RequiredVersion() { return 2011000; }
extern "C" __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() { return &g_plugin; }
extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
  g_edit = host->create_edit_handle();
  host->register_edit_menu_param(L"LayerPack\\空レイヤーを圧縮", nullptr, on_menu);
}
