#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#undef assert
#define assert(expression) do { if (!(expression)) std::abort(); } while (false)

// Include the production implementation so this test exercises the exact
// SDK-facing collection, move, and item-update code without a live editor.
#include "../src/main.cpp"

struct MockObject {
  int layer, start, end;
  const wchar_t* effect;
  int target_count;
};
static std::vector<MockObject> objects;
static std::string item_buffer;
static std::string alias_buffer;
static EDIT_HANDLE mock_handle{};
static EDIT_SECTION mock_section{};

static void get_info(EDIT_INFO* info, int) {
  *info = {};
  info->scene_id = 42;
  info->layer_max = 6;
}
static OBJECT_HANDLE find_object(int layer, int frame) {
  for (auto& object : objects)
    if (object.layer == layer && object.start >= frame) return &object;
  return nullptr;
}
static OBJECT_LAYER_FRAME get_position(OBJECT_HANDLE handle) {
  const auto& object = *static_cast<MockObject*>(handle);
  return {object.layer, object.start, object.end};
}
static LPCSTR get_item(OBJECT_HANDLE handle, LPCWSTR effect, LPCWSTR item) {
  const auto& object = *static_cast<MockObject*>(handle);
  if (!object.effect || std::wcscmp(object.effect, effect) ||
      std::wcscmp(item, L"対象レイヤー数")) return nullptr;
  item_buffer = std::to_string(object.target_count);
  return item_buffer.c_str();
}
static bool set_item(OBJECT_HANDLE handle, LPCWSTR effect, LPCWSTR item, LPCSTR value) {
  auto& object = *static_cast<MockObject*>(handle);
  if (!object.effect || std::wcscmp(object.effect, effect) ||
      std::wcscmp(item, L"対象レイヤー数")) return false;
  object.target_count = std::atoi(value);
  return true;
}
static LPCSTR get_alias(OBJECT_HANDLE handle) {
  const auto& object = *static_cast<MockObject*>(handle);
  alias_buffer = object.effect ? "[Object.0]\neffect.name=x\n対象レイヤー数=" +
                                       std::to_string(object.target_count) + "\n"
                               : "[Object.0]\neffect.name=図形\n";
  return alias_buffer.c_str();
}
static bool move_object(OBJECT_HANDLE handle, int layer, int frame) {
  auto& source = *static_cast<MockObject*>(handle);
  for (const auto& other : objects)
    if (&other != &source && other.layer == layer &&
        !(source.end < other.start || frame > other.end)) return false;
  source.layer = layer;
  source.start = frame;
  return true;
}

int main() {
  objects = {{0, 0, 100, L"グループ制御", 4},
             {2, 0, 100, L"カメラ制御", 2},
             {4, 0, 100, nullptr, 0},
             {6, 0, 100, nullptr, 0}};
  mock_handle.get_edit_info = get_info;
  g_edit = &mock_handle;
  mock_section.find_object = find_object;
  mock_section.get_object_layer_frame = get_position;
  mock_section.get_object_item_value = get_item;
  mock_section.set_object_item_value = set_item;
  mock_section.get_object_alias = get_alias;
  mock_section.move_object = move_object;
  mock_section.get_layer_name = [](int) -> LPCWSTR { return nullptr; };
  mock_section.get_layer_enable = [](int) { return true; };
  mock_section.get_layer_lock = [](int) { return false; };
  mock_section.set_edited_state = []() {};

  Snapshot preview;
  collect(&mock_section, preview);
  assert(preview.error.empty());
  assert(preview.plan.moved_layers == 3);
  assert(preview.plan.scope_count[0] == 2);
  assert(preview.plan.scope_count[1] == 1);
  ApplyRequest request{&preview};
  apply(&request, &mock_section);
  assert(request.error.empty());
  assert(request.moved_objects == 3);
  assert(request.changed_scopes == 2);
  assert(objects[0].layer == 0 && objects[0].target_count == 2);
  assert(objects[1].layer == 1 && objects[1].target_count == 1);
  assert(objects[2].layer == 2 && objects[3].layer == 3);
}
