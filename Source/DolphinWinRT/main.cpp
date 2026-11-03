#include <unknwn.h>  // first, before any winrt header: classic COM interfaces then work with as<>()
#include "Host.h"

#include "UWPUtils.h"
#include "Common/GvTestKeys.h"

#include <windows.h>
#include <iostream>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <share.h>
#include <mutex>
#include <thread>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/windows.graphics.display.core.h>

#include <Gamingdeviceinformation.h>

#include "UICommon/GameFile.h"
#include "UICommon/ImGuiMenu/ImGuiFrontend.h"
#include "UICommon/ImGuiMenu/ImGuiNetplay.h"
#include "UICommon/ImGuiMenu/WinRTKeyboard.h"
#include "UICommon/UICommon.h"

#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/Present.h"

#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/WindowSystemInfo.h"
#include "Common/Flag.h"
#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Core.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/Host.h"
#include "Core/HotkeyManager.h"
#include "Core/IOS/STM/STM.h"

#define SDL_MAIN_HANDLED

using namespace winrt;

using namespace Windows;
using namespace Windows::Storage;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Composition;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;
using namespace Windows::Graphics::Display::Core;

using winrt::Windows::UI::Core::BackRequestedEventArgs;
using winrt::Windows::UI::Core::CoreProcessEventsOption;
using winrt::Windows::UI::Core::CoreWindow;

std::shared_ptr<NetPlay::NetPlayClient> g_netplay_client = nullptr;
std::shared_ptr<NetPlay::NetPlayServer> g_netplay_server = nullptr;
std::shared_ptr<ImGuiFrontend::ImGuiNetPlay> g_netplay_dialog = nullptr;

extern "C" void uwp_redirect_file_apis_startup(void);   // UWPFileRedirect.cpp
extern "C" unsigned uwp_redirect_count;

namespace UWP
{
Common::Flag m_running{false};
winrt::hstring m_launchOnExit;
Common::Flag g_shutdown_requested{false};
Common::Flag g_tried_graceful_shutdown{false};
Common::Flag g_needs_frontend_reset{false};
// Game Vault: set once OnActivate has run (it boots a game handed over by a link)
Common::Flag g_activated{false};

// Game Vault: the game file shared through the link's disctoken= (Common/IOFile.h). A drive this app may not open
// itself (seen with a player's USB drive: Wii games went straight back to the Home screen) is read through it.
// IStorageItemHandleAccess as in the SDK's WindowsStorageCOM.h, which pulls in desktop-only shell headers.
enum { GV_HAO_READ = 0x120089, GV_HSO_SHARE_READ = 0x1, GV_HO_NONE = 0 };
struct __declspec(uuid("5CA296B2-2C25-4D22-B785-B885C8201E6A")) __declspec(novtable)
GV_IStorageItemHandleAccess : public ::IUnknown
{
  virtual HRESULT STDMETHODCALLTYPE Create(DWORD accessOptions, DWORD sharingOptions, DWORD options,
                                           ::IUnknown* oplockBreakingHandler, HANDLE* interopHandle) = 0;
};
winrt::hstring g_share_token;
std::string g_share_path;
std::mutex g_share_lock;
winrt::Windows::Storage::StorageFile g_share_file{nullptr};  // redeemed once, a handle made per open

static std::string ShareKey(std::string path)
{
  for (char& c : path)
    c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return path;
}

// Without a token (Game Vault could not share the file: 0x80070057 on an Xbox One S, GitHub Dolphin-GameVault #1),
// or when it cannot be redeemed, the file is asked for through the Storage API (broadFileSystemAccess), which
// takes another way to the drive than the plain and FromApp file calls that said "does not exist" there.
static void* OpenGameVaultShare(const std::string& path)
{
  if (g_share_path.empty() || ShareKey(path) != g_share_path)
    return nullptr;
  std::lock_guard<std::mutex> lock(g_share_lock);
  HANDLE result = nullptr;
  const bool first = !g_share_file;
  // blocking on WinRT calls is not allowed on the app's UI thread: a thread of its own
  std::thread([&] {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try
    {
      if (!g_share_file && !g_share_token.empty())
      {
        try
        {
          g_share_file = Windows::ApplicationModel::DataTransfer::SharedStorageAccessManager::
                             RedeemTokenForFileAsync(g_share_token)
                                 .get();
        }
        catch (...)
        {
        }
      }
      if (!g_share_file)
      {
        g_share_file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
                           winrt::to_hstring(g_share_path))
                           .get();
        NOTICE_LOG_FMT(CORE, "Game Vault trace: game file reached through the Storage API");
      }
      HANDLE h = nullptr;
      if (SUCCEEDED(g_share_file.as<GV_IStorageItemHandleAccess>()->Create(GV_HAO_READ, GV_HSO_SHARE_READ,
                                                                           GV_HO_NONE, nullptr, &h)))
        result = h;
    }
    catch (...)
    {
    }
    winrt::uninit_apartment();
  }).join();
  if (first || !result)
    NOTICE_LOG_FMT(CORE, "Game Vault trace: shared game file {}", result ? "opened" : "not opened");
  return result;
}

// Game Vault: Dolphin's own log of each start, in the folder this publisher's apps share (games\logs), which Game
// Vault sends with its problem reports. Dolphin's main log is off by default and stays in LocalState, so a start
// that failed said nothing at all (Krauser985, 2026-10-18: Dolphin closed within seconds, from Game Vault and from
// the Home screen alike). Written as it goes, so the last line shows how far a start got.
static std::mutex g_gvlog_lock;
static FILE* g_gvlog = nullptr;
static long g_gvlog_size = 0;
constexpr long GVLOG_MAX = 2 << 20;

static void GvLogRaw(const char* text)
{
  std::unique_lock<std::mutex> lock(g_gvlog_lock, std::try_to_lock);  // never waits: also used while crashing
  if (!lock.owns_lock() || !g_gvlog || g_gvlog_size > GVLOG_MAX)
    return;
  g_gvlog_size += static_cast<long>(std::fputs(text, g_gvlog) >= 0 ? std::strlen(text) : 0);
  if (g_gvlog_size > GVLOG_MAX)
    std::fputs("[log full, the rest left out]\n", g_gvlog);
  std::fflush(g_gvlog);
}

static void GvLog(const std::string& line)
{
  SYSTEMTIME t;
  GetLocalTime(&t);
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  GvLogRaw((stamp + line + "\n").c_str());
}

static std::wstring GvSharedFolder()
{
  try
  {
    return std::wstring(ApplicationData::Current().GetPublisherCacheFolder(L"games").Path());
  }
  catch (...)
  {
    return L"";
  }
}

static void GvLogCalls(CONTEXT ctx)
{
  char text[512];
// the calls that led there, so the code behind a crash can be found (one offset alone often lands in a
// shared helper such as a string copy)
  for (int i = 0; i < 16 && ctx.Rip; i++)
  {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    char where[MAX_PATH] = "?";
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(ctx.Rip), &mod))
    {
      GetModuleFileNameA(mod, where, MAX_PATH);
    }
    const char* short_name = std::strrchr(where, '\\');
    std::snprintf(text, sizeof(text), "  call %d: %s+0x%llX\n", i, short_name ? short_name + 1 : where,
                  static_cast<unsigned long long>(ctx.Rip - reinterpret_cast<DWORD64>(mod)));
    GvLogRaw(text);
    if (!fn)
    {
      // a leaf function: its return address is on top of the stack
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
      continue;
    }
    void* handler_data = nullptr;
    DWORD64 frame = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
  }
}

// a crash: what and where, as an offset into its module, so it can be looked up in the build's symbols
static LONG WINAPI GvCrashFilter(EXCEPTION_POINTERS* info)
{
  const auto* rec = info->ExceptionRecord;
  char module[MAX_PATH] = "?";
  HMODULE base = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCSTR>(rec->ExceptionAddress), &base))
  {
    GetModuleFileNameA(base, module, MAX_PATH);
  }
  const char* name = std::strrchr(module, '\\');
  char text[512];
  std::snprintf(text, sizeof(text),
                "===== CRASH REPORT (Dolphin) =====\nexception 0x%08lX at %s+0x%llX (info %llX %llX)\n",
                rec->ExceptionCode, name ? name + 1 : module,
                static_cast<unsigned long long>(static_cast<const char*>(rec->ExceptionAddress) -
                                                reinterpret_cast<const char*>(base)),
                rec->NumberParameters > 0 ? static_cast<unsigned long long>(rec->ExceptionInformation[0]) : 0ull,
                rec->NumberParameters > 1 ? static_cast<unsigned long long>(rec->ExceptionInformation[1]) : 0ull);
  GvLogRaw(text);

  __try
  {
    GvLogCalls(*info->ContextRecord);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    GvLogRaw("  (call list cut short)\n");
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void GvOpenLog()
{
  if (g_gvlog)
    return;
  const std::wstring shared = GvSharedFolder();
  if (shared.empty())
    return;
  const std::wstring logs = shared + L"\\logs";
  CreateDirectoryW(logs.c_str(), nullptr);
  g_gvlog = _wfsopen((logs + L"\\Dolphin (GameCube, Wii).log").c_str(), L"w", _SH_DENYWR);
  SetUnhandledExceptionFilter(GvCrashFilter);
  std::set_terminate([] {
    GvLog("stopped: an error nobody caught (std::terminate)");
    std::abort();
  });
  const auto v = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
  GvLog(fmt::format("Dolphin for Game Vault {}.{}.{}.{} starting", v.Major, v.Minor, v.Build, v.Revision));
}

// Dolphin's own notices and errors (panic alerts are logged as errors) go into the start log too. The log manager
// is made anew with every UICommon::Init, so this runs after each.
class GvLogListener : public Common::Log::LogListener
{
public:
  void Log(Common::Log::LogLevel, const char* msg) override { GvLogRaw(msg); }
};

static void GvHookLog()
{
  auto* manager = Common::Log::LogManager::GetInstance();
  if (!manager || !g_gvlog)
    return;
  manager->RegisterListener(Common::Log::LogListener::LOG_WINDOW_LISTENER, std::make_unique<GvLogListener>());
  manager->EnableListener(Common::Log::LogListener::LOG_WINDOW_LISTENER, true);
  if (manager->GetLogLevel() < Common::Log::LogLevel::LERROR)
    manager->SetLogLevel(Common::Log::LogLevel::LERROR);
  for (int i = 0; i < static_cast<int>(Common::Log::LogType::NUMBER_OF_LOGS); i++)
  {
    const auto type = static_cast<Common::Log::LogType>(i);
    // a game's own reports can be many lines a second
    if (type != Common::Log::LogType::OSREPORT && type != Common::Log::LogType::OSREPORT_HLE)
      manager->SetEnable(type, true);
  }
}

// Rumble: up to 1.1.9.108 the default controller settings sent rumble to "Rumble 0", an output Xbox pads do not
// have (they rumble through "Motor L"/"Motor R"), so GameCube and Wii games never shook the pad. Settings saved
// with that old default get both motors instead; a rumble line the player changed themselves is left alone.
static void GvRumbleFix()
{
  const std::string config = File::GetUserPath(D_CONFIG_IDX);
  for (const char* name : {"GCPadNew.ini", "WiimoteNew.ini"})
  {
    std::string text;
    if (!File::ReadFileToString(config + name, text))
      continue;
    int changed = 0;
    for (size_t at = 0; (at = text.find("/Xbox One Game Controller:Rumble 0`", at)) != std::string::npos;)
    {
      const size_t open = text.rfind('`', at);
      const size_t close = at + std::strlen("/Xbox One Game Controller:Rumble 0`");
      if (open == std::string::npos)
        break;
      const std::string device = text.substr(open + 1, at - open - 1);  // "WGInput/0"
      const std::string both = "`" + device + "/Xbox One Game Controller:Motor L` | `" + device +
                               "/Xbox One Game Controller:Motor R`";
      text.replace(open, close - open, both);
      at = open + both.size();
      changed++;
    }
    if (changed && File::WriteStringToFile(config + name, text))
      GvLog(fmt::format("rumble: {} line(s) in {} now use both motors", changed, name));
  }
}

// Safe start: LocalState\gv-start.txt holds "<starts in a row that failed> <times the settings were set aside>".
// A start counts as good once Dolphin has run 20 s or closed normally. After two failed starts in a row the
// settings (the Config folder, possibly copied over from the original Dolphin by the setup) are set aside once,
// and the game list cache is dropped, so Dolphin starts with its defaults. Saves, memory cards and the Wii data
// stay where they are.
static std::string GvStartFile()
{
  return UWP::GetLocalFolder() + "\\gv-start.txt";
}

static void GvStartGood()
{
  static Common::Flag done{false};
  if (done.TestAndSet())
  {
    int fails = 0, asides = 0;
    if (FILE* f = std::fopen(GvStartFile().c_str(), "r"))
    {
      std::fscanf(f, "%d %d", &fails, &asides);
      std::fclose(f);
    }
    if (FILE* f = std::fopen(GvStartFile().c_str(), "w"))
    {
      std::fprintf(f, "0 %d\n", asides);
      std::fclose(f);
    }
    GvLog("start: good");
  }
}

static void GvSafeStart(const std::string& user)
{
  int fails = 0, asides = 0;
  if (FILE* f = std::fopen(GvStartFile().c_str(), "r"))
  {
    std::fscanf(f, "%d %d", &fails, &asides);
    std::fclose(f);
  }
  GvLog(fmt::format("start: {} failed start(s) before this one, settings set aside {} time(s) so far", fails,
                    asides));
  if (fails >= 2 && asides == 0)
  {
    SYSTEMTIME t;
    GetLocalTime(&t);
    const std::string config = user + "/Config";
    const std::string aside = fmt::format("{}/Config set aside {:04}-{:02}-{:02} {:02}{:02}{:02}", user, t.wYear,
                                          t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const bool moved = File::IsDirectory(config) && File::Rename(config, aside);
    File::Delete(user + "/Cache/gamelist.cache", File::IfAbsentBehavior::NoConsoleWarning);
    GvLog(moved ? "start: settings set aside to \"" + aside + "\", starting with the defaults" :
                  "start: no settings to set aside (" + config + ")");
    asides++;
    fails = 0;
  }
  if (FILE* f = std::fopen(GvStartFile().c_str(), "w"))
  {
    std::fprintf(f, "%d %d\n", fails + 1, asides);
    std::fclose(f);
  }
  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(20));
    GvStartGood();
  }).detach();
}

// The user folder chosen in Dolphin's settings (LocalState\user.txt) may be on a drive that is gone or that this
// app may not write to; Dolphin then cannot keep its settings and closes. This start then uses LocalState.
static std::string GvUserLocation()
{
  std::string user = UWP::GetUserLocation();
  while (!user.empty() && (user.back() == '\r' || user.back() == '\n' || user.back() == ' '))
    user.pop_back();
  const std::string local = UWP::GetLocalFolder();
  if (user != local)
  {
    const bool ok = !user.empty() && File::IsDirectory(user) && UWP::TestPathPermissions(user);
    GvLog("user folder from Dolphin's settings: " + user + (ok ? "" : " - cannot be written to, using LocalState"));
    if (!ok)
      return local;
  }
  return user;
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
  IFrameworkView CreateView() { return *this; }

  void Initialize(CoreApplicationView const& v)
  {
    v.Activated({this, &App::OnActivate});
    CoreApplication::EnteredBackground({this, &App::EnteredBackground});
    CoreApplication::Suspending({this, &App::Suspending});
  }

  void Load(hstring const&) {}

  void Uninitialize() {}

  void Run()
  {
    GvTest::Watch(UWP::GetLocalFolder() + "\\gvtest_keys.txt",   // test hook: Common/GvTestKeys.h
                  [](const std::string& line) { GvLog(line); });
    // Game Vault: wait for the activation before choosing between the menu and a game. A link's activation often
    // arrives after the first pump; the menu was then already running, and booting the game from OnActivate shut
    // down the video backend the menu still drew with (null g_vertex_manager in VertexManagerBase::Flush).
    for (int waited = 0; !g_activated.IsSet() && waited < 5000; waited += 10)
    {
      CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(
          winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
      if (!g_activated.IsSet())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto navigation = winrt::Windows::UI::Core::SystemNavigationManager::GetForCurrentView();
    navigation.BackRequested(
        [](const winrt::Windows::Foundation::IInspectable&,
           const winrt::Windows::UI::Core::BackRequestedEventArgs& args) { args.Handled(true); });

    Core::DeclareAsHostThread();

    while (true)
    {
      // ImGUI frontend
      if (!m_running.IsSet() || g_needs_frontend_reset.TestAndClear())
      {
        NOTICE_LOG_FMT(CORE, "Game Vault trace: starting the menu (running {})", m_running.IsSet());
        // Ensure we're fully cleaned up before starting new frontend
        if (Core::IsRunning(Core::System::GetInstance()))
        {
          Core::Stop(Core::System::GetInstance());
        }
        Core::Shutdown(Core::System::GetInstance());

        // Reset flags
        g_shutdown_requested.Clear();
        g_tried_graceful_shutdown.Clear();

        // Reinitialize UI systems
        UICommon::Shutdown();
        UICommon::Init();
        GvHookLog();
        UICommon::InitControllers({});

        // Create and run new frontend
        GvLog("step: menu starting");
        auto frontend = new ImGuiFrontend::ImGuiFrontend();
        auto result = frontend->RunUntilSelection();
        auto game = result.game_result;
        GvLog(game ? "menu: game chosen" : result.netplay ? "menu: netplay" :
              result.boot_params ? "menu: system menu" : "menu: closed without a choice");

        if (game)
        {
          InitializeDolphinFromFile(game->GetFilePath());
        }
        else if (result.netplay)
        {
          InitializeDolphinForNetplay();
        }
        else if (result.boot_params)
        {
          auto& system = Core::System::GetInstance();
          if (Core::IsRunning(system))
            Core::Stop(system);
          Core::Shutdown(system);
          // Prepare window info
          CoreWindow window = CoreWindow::GetForCurrentThread();
          void* abi = winrt::get_abi(window);
          WindowSystemInfo wsi;
          wsi.type = WindowSystemType::Windows;
          wsi.render_surface = abi;
          wsi.render_width = window.Bounds().Width;
          wsi.render_height = window.Bounds().Height;
          // Boot via BootManager
          if (!BootManager::BootCore(system, std::move(result.boot_params), wsi))
          {
            fprintf(stderr, "Could not boot system menu\n");
            Core::Shutdown(system);
          }
          else
          {
            m_running.Set();
          }
        }

        delete frontend;
      }

      g_tried_graceful_shutdown.Clear();

      // Dolphin loop
      while (Core::GetState(Core::System::GetInstance()) != Core::State::Stopping)
      {
        auto& system = Core::System::GetInstance();

        if (g_shutdown_requested.TestAndClear())
        {
          NOTICE_LOG_FMT(CORE, "Game Vault trace: shutdown requested");
          if (NetPlay::IsNetPlayRunning())
            NetPlay::SendPowerButtonEvent();

          Core::Stop(system);
          Core::Shutdown(system);
          m_running.Clear();

          // Only set frontend reset after proper cleanup
          g_needs_frontend_reset.Set();
          break;
        }

        ::Core::HostDispatchJobs(system);

        if (Core::IsRunningOrStarting(Core::System::GetInstance()))
        {
          Core::UpdateInputGate(false);
          HotkeyManagerEmu::GetStatus(false);
          ControlReference::SetInputGate(true);

          HotkeyManagerEmu::GetStatus(true);

          if (HotkeyManagerEmu::IsPressed(HK_TOGGLE_ONSCREEN_MENU, false))
          {
            OSD::ToggleShowSettings();
            Core::SetState(Core::System::GetInstance(),
                           Core::GetState(Core::System::GetInstance()) == Core::State::Paused ?
                               Core::State::Running :
                               Core::State::Paused);
          }

          if (Core::GetState(Core::System::GetInstance()) == Core::State::Paused)
          {
            g_presenter->Present();
          }
        }

        CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(
            CoreProcessEventsOption::ProcessAllIfPresent);

        constexpr u32 INTERVAL_MS = 1000 / 30;
        constexpr auto INTERVAL = std::chrono::milliseconds(INTERVAL_MS);
        std::this_thread::sleep_for(INTERVAL);
      }

      // Make sure we've shut down properly.
      NOTICE_LOG_FMT(CORE, "Game Vault trace: game loop ended, state {}",
                     static_cast<int>(Core::GetState(Core::System::GetInstance())));
      m_running.Clear();

      // If there's another frontend, boot to that.
      if (!m_launchOnExit.empty())
      {
        winrt::Windows::Foundation::Uri m_uri{m_launchOnExit};
        auto asyncOperation = winrt::Windows::System::Launcher::LaunchUriAsync(m_uri);
        asyncOperation.Completed([](winrt::Windows::Foundation::IAsyncOperation<bool> const& sender,
                                    winrt::Windows::Foundation::AsyncStatus const asyncStatus) {
          CoreApplication::Exit();
          return;
        });
      }

      // Continue looping to open the frontend again
    }
  }

  void InitializeDolphinForNetplay() { m_running.Set(); }

  winrt::fire_and_forget InitializeDolphinFromFile(std::string path)
  {
    NOTICE_LOG_FMT(CORE, "Game Vault trace: InitializeDolphinFromFile '{}'", path);
    if (path.empty())
    {
      FileOpenPicker openPicker;
      openPicker.ViewMode(PickerViewMode::List);
      openPicker.SuggestedStartLocation(PickerLocationId::HomeGroup);
      openPicker.FileTypeFilter().Append(L".iso");
      openPicker.FileTypeFilter().Append(L".ciso");
      openPicker.FileTypeFilter().Append(L".rvz");
      openPicker.FileTypeFilter().Append(L".wbfs");
      openPicker.FileTypeFilter().Append(L".gcm");
      openPicker.FileTypeFilter().Append(L".gcz");
      openPicker.FileTypeFilter().Append(L".json");
      openPicker.FileTypeFilter().Append(L".elf");
      openPicker.FileTypeFilter().Append(L".dol");
      openPicker.FileTypeFilter().Append(L".tgc");
      openPicker.FileTypeFilter().Append(L".wad");

      auto file = co_await openPicker.PickSingleFileAsync();
      if (file)
      {
        path = winrt::to_string(file.Path().data());
      }
    }

    CoreWindow window = CoreWindow::GetForCurrentThread();
    void* abi = winrt::get_abi(window);

    WindowSystemInfo wsi;
    wsi.type = WindowSystemType::Windows;
    wsi.render_surface = abi;
    wsi.render_width = window.Bounds().Width;
    wsi.render_height = window.Bounds().Height;

    GAMING_DEVICE_MODEL_INFORMATION info = {};
    GetGamingDeviceModelInformation(&info);
    if (info.vendorId == GAMING_DEVICE_VENDOR_ID_MICROSOFT)
    {
      HdmiDisplayInformation hdi = HdmiDisplayInformation::GetForCurrentView();
      if (hdi)
      {
        wsi.render_width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();
        wsi.render_height = hdi.GetCurrentDisplayMode().ResolutionHeightInRawPixels();
        // Our UI is based on 1080p, and we're adding a modifier to zoom in by 80%
        wsi.render_surface_scale = ((float)wsi.render_width / 1920.0f) * 1.8f;
      }
    }

    auto& system = Core::System::GetInstance();

    // Ensure clean state before booting
    Core::Shutdown(system);

    std::unique_ptr<BootParameters> boot =
        BootParameters::GenerateFromFile(path, BootSessionData("", DeleteSavestateAfterBoot::No));

    GvLog(fmt::format("boot: {} ({}x{})", path, wsi.render_width, wsi.render_height));
    if (!boot)
    {
      // the game file could not be opened (gone, or no access): Dolphin itself is fine, so this is not a failed
      // start - two of them set a player's settings aside for nothing (a missing Wind Waker .ciso)
      GvLog("boot: the game file could not be opened - not counted as a failed start");
      GvStartGood();
      Core::Shutdown(system);
      co_return;
    }
    if (!BootManager::BootCore(system, std::move(boot), wsi))
    {
      GvLog("boot: failed (Dolphin's reason is in the lines above)");
      fprintf(stderr, "Could not boot the specified file\n");
      Core::Shutdown(system);
      co_return;
    }

    GvLog("boot: started");
    m_running.Set();
    co_return;
  }

  void OnClosed(const IInspectable&, const winrt::Windows::UI::Core::CoreWindowEventArgs& args)
  {
    GvLog("closed");
    GvStartGood();
    g_shutdown_requested.Set();
  }

  void OnActivate(const winrt::Windows::ApplicationModel::Core::CoreApplicationView&,
                  const winrt::Windows::ApplicationModel::Activation::IActivatedEventArgs& args)
  {
    std::stringstream filePath;
    GvOpenLog();
    GvLog(fmt::format("activated: kind {}, previous state {}", static_cast<int>(args.Kind()),
                      static_cast<int>(args.PreviousExecutionState())));
    NOTICE_LOG_FMT(CORE, "Game Vault trace: OnActivate kind {} previous {}", static_cast<int>(args.Kind()),
                   static_cast<int>(args.PreviousExecutionState()));

    if (args.Kind() == Windows::ApplicationModel::Activation::ActivationKind::Protocol)
    {
      auto protocolActivatedEventArgs{
          args.as<Windows::ApplicationModel::Activation::ProtocolActivatedEventArgs>()};
      auto query = protocolActivatedEventArgs.Uri().QueryParsed();

      for (uint32_t i = 0; i < query.Size(); i++)
      {
        auto arg = query.GetAt(i);

        // parse command line string
        if (arg.Name() == winrt::hstring(L"cmd"))
        {
          std::string argVal = winrt::to_string(arg.Value());

          // Strip the executable from the cmd argument
          if (argVal.starts_with("dolphin.exe"))
          {
            argVal = argVal.substr(11, argVal.length());
          }

          std::istringstream iss(argVal);
          std::string s;

          // Maintain slashes while reading the quotes
          while (iss >> std::quoted(s, '"', (char)0))
          {
            filePath << s;
          }
        }
        else if (arg.Name() == winrt::hstring(L"launchOnExit"))
        {
          // For if we want to return to a frontend
          m_launchOnExit = arg.Value();
        }
        else if (arg.Name() == winrt::hstring(L"disctoken"))
        {
          g_share_token = arg.Value();
        }
      }
    }

    // Game Vault: App type "App" (Dev Home) gives only 1 GB, and games run out of memory and close. Started by
    // Game Vault with that little: straight back to it with "from=lowmem-dolphin", which says on the TV
    // what to change.
    if (std::wstring_view(m_launchOnExit).rfind(L"gamevault:", 0) == 0 &&
        winrt::Windows::System::MemoryManager::AppMemoryUsageLimit() < (2048ull << 20))
    {
      GvLog("app type: only " + std::to_string(winrt::Windows::System::MemoryManager::AppMemoryUsageLimit() >> 20) +
            " MB (App), back to Game Vault");
      winrt::Windows::System::Launcher::LaunchUriAsync(
          winrt::Windows::Foundation::Uri(L"gamevault:?from=lowmem-dolphin"))
          .Completed([](auto const&, auto) { winrt::Windows::ApplicationModel::Core::CoreApplication::Exit(); });
      return;
    }

    Core::DeclareAsHostThread();

    GvLog(fmt::format("link: game \"{}\", shared file {}, back to {}, file calls through the broker {}",
                      filePath.str(), g_share_token.empty() ? "no" : "yes",
                      m_launchOnExit.empty() ? "nothing" : "a link", uwp_redirect_count));
    const std::string user = GvUserLocation();
    static Common::Flag safe_start_done{false};
    if (safe_start_done.TestAndSet())
      GvSafeStart(user);
    UICommon::SetUserDirectory(user);
    GvLog("step: user folder " + File::GetUserPath(D_USER_IDX));
    UICommon::CreateDirectories();
    GvRumbleFix();
    UICommon::Init();
    GvHookLog();
    GvLog("step: settings loaded");
    UICommon::InitControllers({});
    GvLog("step: controllers ready");

    std::string gamePath = filePath.str();
    if (!gamePath.empty())   // the file through the share token or the Storage API, when plain calls fail
    {
      // test switch LocalState\share-test.txt: boot a path that cannot be opened, so the game is read the way a
      // refused drive's is (only through the shared file)
      if (!g_share_token.empty() && File::Exists(File::GetUserPath(D_USER_IDX) + "share-test.txt"))
      {
        gamePath = "Z:\\gv-share-test\\" + gamePath.substr(gamePath.find_last_of("\\/") + 1);
        NOTICE_LOG_FMT(CORE, "Game Vault trace: share test, booting {}", gamePath);
      }
      g_share_path = ShareKey(gamePath);
      File::SetSharedFileOpener(OpenGameVaultShare);
    }
    if (!gamePath.empty() && gamePath != "")
    {
      InitializeDolphinFromFile(gamePath);
    }

    CoreWindow window = CoreWindow::GetForCurrentThread();
    window.Activate();
    g_activated.Set();
  }

  void EnteredBackground(const IInspectable&,
                         const winrt::Windows::ApplicationModel::EnteredBackgroundEventArgs& args)
  {
  }

  void Suspending(const IInspectable&,
                  const winrt::Windows::ApplicationModel::SuspendingEventArgs& args)
  {
    auto& system = Core::System::GetInstance();
    NOTICE_LOG_FMT(CORE, "Game Vault trace: Suspending");
    GvLog("suspending (closed by the console or the player)");
    GvStartGood();

    // The Series S/X quits fast, so let's immediately shutdown to ensure all the caches save.
    Core::Stop(system);
    Core::Shutdown(system);
    UICommon::Shutdown();

    if (!m_launchOnExit.empty())
    {
      winrt::Windows::Foundation::Uri m_uri{m_launchOnExit};
      auto asyncOperation = winrt::Windows::System::Launcher::LaunchUriAsync(m_uri);
      asyncOperation.Completed([](winrt::Windows::Foundation::IAsyncOperation<bool> const& sender,
                                  winrt::Windows::Foundation::AsyncStatus const asyncStatus) {
        CoreApplication::Exit();
      });
    }
  }

  void OnCharacterReceived(winrt::Windows::UI::Core::CoreWindow const& /* sender */,
                           winrt::Windows::UI::Core::CharacterReceivedEventArgs const& e /* args */)
  {
    UWP::HandleCharacter(e.KeyCode());
  }

  void SetWindow(CoreWindow const& w)
  {
    w.Closed({this, &App::OnClosed});
    w.CharacterReceived({this, &App::OnCharacterReceived});
  }
};
}  // namespace UWP

int WINAPIV WinMain()
{
  // UWPFileRedirect.cpp: file calls go through the FromApp versions, so a drive the app container has no rights
  // to itself (a player's exFAT USB drive: "the specified file does not exist") is read through the broker
  uwp_redirect_file_apis_startup();
  winrt::init_apartment();

  CoreApplication::Run(make<UWP::App>());

  winrt::uninit_apartment();

  return 0;
}

std::vector<std::string> Host_GetPreferredLocales()
{
  return {};
}

void Host_JitCacheInvalidation()
{
}

void Host_JitProfileDataWiped()
{
}

void Host_NotifyMapLoaded()
{
}

void Host_PPCSymbolsChanged()
{
}

void Host_PPCBreakpointsChanged()
{
}

void Host_RefreshDSPDebuggerWindow()
{
}

bool Host_UIBlocksControllerState()
{
  return false;
}

void Host_Message(HostMessageID id)
{
}

void Host_UpdateTitle(const std::string& title)
{
}

void Host_UpdateDisasmDialog()
{
}

void Host_UpdateMainFrame()
{
}

void Host_RequestRenderWindowSize(int width, int height)
{
}

bool Host_RendererHasFocus()
{
  return true;
}

bool Host_RendererHasFullFocus()
{
  // Mouse capturing isn't implemented
  return Host_RendererHasFocus();
}

bool Host_RendererIsFullscreen()
{
  return true;
}

void Host_YieldToUI()
{
}

bool Host_TASInputHasFocus()
{
  return false;
}

void Host_TitleChanged()
{
}

void Host_UpdateDiscordClientID(const std::string& client_id)
{
}

bool Host_UpdateDiscordPresenceRaw(const std::string& details, const std::string& state,
                                   const std::string& large_image_key,
                                   const std::string& large_image_text,
                                   const std::string& small_image_key,
                                   const std::string& small_image_text,
                                   const int64_t start_timestamp, const int64_t end_timestamp,
                                   const int party_size, const int party_max)
{
  return false;
}

std::unique_ptr<GBAHostInterface> Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core> core)
{
  return nullptr;
}
