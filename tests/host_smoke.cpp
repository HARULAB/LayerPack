#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

#define RequiredVersion LayerPackOriginalRequiredVersion
#define GetCommonPluginTable LayerPackOriginalGetCommonPluginTable
#define RegisterPlugin LayerPackOriginalRegisterPlugin
#include "../src/main.cpp"
#undef RequiredVersion
#undef GetCommonPluginTable
#undef RegisterPlugin

namespace {
HINSTANCE smoke_dll{};
HWND smoke_window{};
bool smoke_started = false;
COMMON_PLUGIN_TABLE smoke_table{L"LayerPack Host Smoke", L"Isolated integration test"};
constexpr UINT kRun = WM_APP + 140;

std::wstring output_path() {
  wchar_t path[32768]{};
  GetModuleFileNameW(smoke_dll, path, 32768);
  return (std::filesystem::path(path).parent_path().parent_path() / L"layer-pack-smoke.txt").wstring();
}

std::wstring project_path() {
  wchar_t path[32768]{};
  GetModuleFileNameW(smoke_dll, path, 32768);
  return (std::filesystem::path(path).parent_path().parent_path() / L"layer-pack-test.aup2").wstring();
}

void write_result(const std::string& text) {
  std::ofstream file(std::filesystem::path(output_path()), std::ios::binary);
  file << text;
}

void run_test() {
  smoke_started = true;
  bool created = g_edit->call_edit_section([](EDIT_SECTION* edit) {
    OBJECT_HANDLE group = edit->create_object(L"グループ制御", 0, 0, 30);
    OBJECT_HANDLE camera = edit->create_object(L"カメラ制御", 2, 0, 30);
    OBJECT_HANDLE shape1 = edit->create_object(L"図形", 4, 0, 30);
    OBJECT_HANDLE shape2 = edit->create_object(L"図形", 6, 0, 30);
    if (!group || !camera || !shape1 || !shape2) return;
    edit->set_object_item_value(group, L"グループ制御", L"対象レイヤー数", "4");
    edit->set_object_item_value(camera, L"カメラ制御", L"対象レイヤー数", "2");
  });
  if (!created) { write_result("FAIL create section\n"); return; }
  Snapshot preview;
  if (!g_edit->call_read_section_param(&preview, [](void* raw, EDIT_SECTION* edit) {
        collect(edit, *static_cast<Snapshot*>(raw));
      })) { write_result("FAIL read section\n"); return; }
  if (!preview.error.empty()) { write_result("FAIL collect\n"); return; }
  ApplyRequest request{&preview};
  if (!g_edit->call_edit_section_param(&request, apply) || !request.error.empty()) {
    write_result("FAIL apply\n"); return;
  }
  Snapshot after;
  if (!g_edit->call_read_section_param(&after, [](void* raw, EDIT_SECTION* edit) {
        collect(edit, *static_cast<Snapshot*>(raw));
      })) { write_result("FAIL verify read\n"); return; }
  if (!after.error.empty() || after.objects.size() != 4 || after.controls.size() != 2 ||
      request.moved_objects != 3 || request.changed_scopes != 2 ||
      after.objects[0].location.layer != 0 || after.objects[1].location.layer != 1 ||
      after.objects[2].location.layer != 2 || after.objects[3].location.layer != 3 ||
      after.controls[0].original_count != 2 || after.controls[1].original_count != 1) {
    write_result("FAIL verify values\n"); return;
  }
  if (!g_edit->save_project_file(project_path().c_str()) ||
      !g_edit->open_project_file(project_path().c_str(), false)) {
    write_result("FAIL save or reopen\n"); return;
  }
  Snapshot reopened;
  if (!g_edit->call_read_section_param(&reopened, [](void* raw, EDIT_SECTION* edit) {
        collect(edit, *static_cast<Snapshot*>(raw));
      }) || !reopened.error.empty() || reopened.objects.size() != 4 ||
      reopened.controls.size() != 2 ||
      reopened.objects[0].location.layer != 0 || reopened.objects[1].location.layer != 1 ||
      reopened.objects[2].location.layer != 2 || reopened.objects[3].location.layer != 3 ||
      reopened.controls[0].original_count != 2 || reopened.controls[1].original_count != 1) {
    write_result("FAIL reopen values\n"); return;
  }
  write_result("PASS group=2 camera=1 layers=0,1,2,3 save/reopen\n");
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  if (message == kRun) { run_test(); return 0; }
  return DefWindowProcW(hwnd, message, wp, lp);
}

void project_loaded(PROJECT_FILE*) {
  if (smoke_window && !smoke_started) PostMessageW(smoke_window, kRun, 0, 0);
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) smoke_dll = instance;
  return TRUE;
}
extern "C" __declspec(dllexport) DWORD RequiredVersion() { return 2011000; }
extern "C" __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() { return &smoke_table; }
extern "C" __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
  g_edit = host->create_edit_handle();
  WNDCLASSEXW klass{};
  klass.cbSize = sizeof(klass);
  klass.hInstance = smoke_dll;
  klass.lpfnWndProc = window_proc;
  klass.lpszClassName = L"LayerPack.HostSmoke.Window";
  if (RegisterClassExW(&klass))
    smoke_window = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, smoke_dll, nullptr);
  host->register_project_load_handler(project_loaded);
}
