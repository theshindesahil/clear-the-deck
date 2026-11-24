// Tests for game-mode-toggle.wh.cpp. Built outside Windhawk with the stub API (WH_EDITING).
//
//   test.exe unit            pure logic only
//   test.exe probe           read-only look at this PC (no app is touched)
//   test.exe live <tests>    turn on/off against the fake apps in <tests>\bin (never real apps)
//
// The live part refuses to run unless every app it may close is a GmFake* program and
// closeOthers is off.

#include <cstdio>
#include <functional>

static size_t TestStoragePath(PWSTR buf, size_t n);
#define Wh_GetModStoragePath TestStoragePath
#include "../game-mode-toggle.wh.cpp"

static std::wstring g_storage;
static size_t TestStoragePath(PWSTR buf, size_t n) {
    if (g_storage.empty() || g_storage.size() + 1 > n) {
        return 0;
    }
    wcscpy(buf, g_storage.c_str());
    return g_storage.size();
}

static int g_pass = 0;
static int g_fail = 0;

static void Out(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string b(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), b.data(), n, nullptr, nullptr);
    fwrite(b.data(), 1, b.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void Check(bool ok, const std::wstring& what) {
    (ok ? g_pass : g_fail)++;
    Out(std::wstring(ok ? L"  PASS  " : L"  FAIL  ") + what);
}

// ---------------------------------------------------------------------------------------------
static void UnitTests() {
    Out(L"== hotkey parsing");
    UINT m = 0, k = 0;
    Check(ParseHotkey(L"Ctrl+Alt+G", &m, &k) && m == (MOD_CONTROL | MOD_ALT) && k == 'G', L"Ctrl+Alt+G");
    Check(ParseHotkey(L"  ctrl + shift + F12 ", &m, &k) && m == (MOD_CONTROL | MOD_SHIFT) && k == VK_F12, L"spaces and F12");
    Check(ParseHotkey(L"Win+Space", &m, &k) && m == MOD_WIN && k == VK_SPACE, L"Win+Space");
    Check(ParseHotkey(L"alt+1", &m, &k) && k == '1', L"digit");
    Check(ParseHotkey(L"Control+Windows+f24", &m, &k) && k == VK_F24, L"long names, F24");
    for (const wchar_t* bad : {L"G", L"", L"Ctrl+", L"Ctrl+Alt", L"Ctrl+Alt+G+H", L"Ctrl+F25", L"Ctrl+F0", L"Ctrl+Esc", L"+G", L"Ctrl++G"}) {
        Check(!ParseHotkey(bad, &m, &k), std::wstring(L"rejects \"") + bad + L"\"");
    }

    Out(L"== command line arguments");
    Check(ArgsFromCommandLine(L"\"C:\\a b\\x.exe\" -silent --x \"y z\"") == L"-silent --x \"y z\"", L"quoted exe");
    Check(ArgsFromCommandLine(L"x.exe -a") == L"-a", L"unquoted exe");
    Check(ArgsFromCommandLine(L"\"C:\\x.exe\"") == L"", L"no arguments");
    Check(ArgsFromCommandLine(L"x.exe") == L"", L"bare exe");
    Check(ArgsFromCommandLine(L"zen.exe -osint -url https://github.com/login?x=1") == L"", L"link handler start dropped");
    Check(ArgsFromCommandLine(L"\"C:\\x.exe") == L"", L"unterminated quote");

    Out(L"== names and folders");
    Check(ExeKey(L" Rainmeter.EXE ") == L"rainmeter" && ExeKey(L"Rainmeter") == L"rainmeter", L"ExeKey");
    Check(InNameList({L"Rainmeter.exe"}, L"rainmeter") && !InNameList({L"Rainmeter.exe"}, L"rainmeter2.exe"), L"InNameList");
    Check(InFolder(L"D:\\Games\\a\\b.exe", L"d:\\games") && InFolder(L"D:\\Games\\a.exe", L"D:\\Games\\"), L"InFolder, case and trailing slash");
    Check(!InFolder(L"D:\\Games2\\a.exe", L"D:\\Games") && !InFolder(L"D:\\Games", L"D:\\Games") && !InFolder(L"D:\\a.exe", L""), L"InFolder sibling, itself, empty");

    Out(L"== which apps are kept");
    Settings s;
    s.keepApps = {L"steam.exe", L"Discord"};
    s.gameFolders = {L"D:\\SteamLibrary\\steamapps\\common"};
    const std::wstring win = L"C:\\Windows";
    Check(WhyKeep(L"explorer.exe", L"C:\\Windows\\explorer.exe", win, s) != nullptr, L"explorer kept");
    Check(WhyKeep(L"windhawk.exe", L"C:\\Program Files\\Windhawk\\windhawk.exe", win, s) != nullptr, L"windhawk kept");
    Check(WhyKeep(L"foo.exe", L"C:\\Windows\\System32\\foo.exe", win, s) != nullptr, L"anything under the Windows folder kept");
    Check(WhyKeep(L"Steam.exe", L"C:\\Program Files (x86)\\Steam\\steam.exe", win, s) != nullptr, L"keepApps, any case");
    Check(WhyKeep(L"discord.exe", L"C:\\x\\discord.exe", win, s) != nullptr, L"keepApps without .exe");
    Check(WhyKeep(L"game.exe", L"D:\\SteamLibrary\\steamapps\\common\\g\\game.exe", win, s) != nullptr, L"game folder kept");
    Check(WhyKeep(L"chrome.exe", L"C:\\Program Files\\Google\\Chrome\\chrome.exe", win, s) == nullptr, L"an ordinary app may be closed");
    Check(WhyKeep(L"chrome.exe", L"C:\\Windowsx\\chrome.exe", win, s) == nullptr, L"C:\\Windowsx is not the Windows folder");
    s.keepFolders = {L"\\Windhawk\\", L"\\NVIDIA Corporation\\", L""};
    Check(WhyKeep(L"VSCodium.exe", L"C:\\Program Files\\Windhawk\\UI\\VSCodium\\VSCodium.exe", win, s) != nullptr, L"the Windhawk editor is kept by folder");
    Check(WhyKeep(L"x.exe", L"C:\\Program Files\\NVIDIA Corporation\\x.exe", win, s) != nullptr, L"keepFolders matches a piece of the path");
    Check(WhyKeep(L"x.exe", L"C:\\Program Files\\Other\\x.exe", win, s) == nullptr, L"an empty keepFolders entry matches nothing");
    Check(LooksLikeHelper(L"GoogleUpdate.exe") && LooksLikeHelper(L"crashpad_handler.exe") && LooksLikeHelper(L"setup.exe") && !LooksLikeHelper(L"PhoneExperienceHost.exe") && !LooksLikeHelper(L"zen.exe"), L"updaters and crash handlers are recognised as helpers");
    Check(IsGuiExe(L"C:\\Windows\\System32\\notepad.exe") && !IsGuiExe(L"C:\\Windows\\System32\\cmd.exe") && !IsGuiExe(L"C:\\no\\such.exe"), L"GUI and console programs are told apart");
    Check(DisplayName(L"C:\\Windows\\System32\\notepad.exe") == L"Notepad" && DisplayName(L"C:\\no\\such\\Thing.exe") == L"Thing", L"friendly name from the file, else the file name");

    Out(L"== manufacturer names");
    std::vector<std::wstring> vend = {L"Acer", L"Lenovo", L"HP Inc", L"Hewlett-Packard", L"Dell", L"MSI", L"Intel", L"", L"  "};
    Check(VendorMatches(L"Acer Incorporated", vend) && VendorMatches(L"ACER inc", vend) && VendorMatches(L"Lenovo (Beijing) Limited", vend), L"company names match, any capitalization");
    Check(VendorMatches(L"HP Inc.", vend) && VendorMatches(L"Hewlett-Packard Company", vend) && VendorMatches(L"Dell Inc.", vend) && VendorMatches(L"Micro | msi", vend), L"HP, Dell and a name inside a longer text match");
    Check(!VendorMatches(L"Racer Games Ltd", vend) && !VendorMatches(L"Microsoft Corporation", vend) && !VendorMatches(L"Google LLC", vend) && !VendorMatches(L"", vend), L"a name inside another word, other companies and empty text do not match");
    Check(!VendorMatches(L"Anything", {L"", L"   "}), L"empty vendor entries match nothing");
    Check(!VendorMatches(L"Rogue Squadron", {L"ROG"}) && VendorMatches(L"ASUS ROG Armoury", {L"ROG"}) && !VendorMatches(L"Samsung | amdahl", {L"AMD"}) && VendorMatches(L"x | amd | y", {L"AMD"}), L"names of 3 letters or fewer must be a whole word");
    Check(InstallFolderName(L"C:\\Program Files (x86)\\Acer\\Care Center\\ACCStd.exe") == L"acer" &&
              InstallFolderName(L"C:\\Program Files\\WindowsApps\\ULICTekInc.PredatorSense_5\\app\\x.exe") == L"ulictekinc.predatorsense_5" &&
              InstallFolderName(L"C:\\Users\\Dell\\AppData\\Local\\Zen\\zen.exe") == L"",
          L"the maker is read from the install folder only, never from a user name in the path");
    Check(VendorMatches(L" | PredatorSense | ulictekinc.predatorsensefornotebook", {L"ULIC", L"Predator"}) && VendorMatches(L" | StorPSCTL | acer", {L"Acer"}), L"PredatorSense and the Acer tools are recognised");

    Out(L"== snapshot format");
    Snapshot a;
    a.startedAt = 133800000000000000ULL;
    a.plan = L"19799241-b240-4294-9c2c-43be1373d0ed";
    a.apps.push_back({L"Zen", L"C:\\Program Files\\Zen Browser\\zen.exe", L"-P \"my profile\"\twith tab\nand newline\\and backslash", L""});
    a.apps.push_back({L"WhatsApp", L"C:\\Program Files\\WindowsApps\\x\\WhatsApp.exe", L"", L"5319275A.WhatsAppDesktop_cv1g1gvanyjgm!App"});
    a.prios.push_back({1234, L"game.exe", 0x20});
    std::wstring text = SerializeSnapshot(a);
    Snapshot b;
    bool ok = ParseSnapshot(text, &b);
    Check(ok, L"round trip parses");
    Check(ok && b.startedAt == a.startedAt && b.plan == a.plan && b.apps.size() == 2 && b.prios.size() == 1, L"round trip keeps the counts");
    Check(ok && b.apps[0].args == a.apps[0].args && b.apps[1].aumid == a.apps[1].aumid && b.prios[0].priorityClass == 0x20, L"tabs, newlines and backslashes survive");
    // Only the text minus its final newline may parse: the end marker is there, so nothing is missing.
    int prefixOk = 0;
    size_t acceptedAt = 0;
    for (size_t n = 0; n < text.size(); n++) {
        Snapshot t;
        if (ParseSnapshot(text.substr(0, n), &t)) {
            prefixOk++;
            acceptedAt = n;
        }
    }
    Check(prefixOk == 1 && acceptedAt == text.size() - 1,
          L"every truncated prefix that lost data is rejected (" + std::to_wstring(text.size()) + L" prefixes tried)");
    Snapshot t;
    Check(!ParseSnapshot(text + L"app\tx\ty\tz\tw\n", &t), L"data after the end marker is rejected");
    Check(!ParseSnapshot(L"", &t) && !ParseSnapshot(L"not a snapshot", &t) && !ParseSnapshot(L"GMT2\nend\n", &t), L"garbage and unknown versions are rejected");
    Check(!ParseSnapshot(L"GMT1\nstarted\tabc\nend\n", &t) && !ParseSnapshot(L"GMT1\nend\n", &t), L"missing or bad start time is rejected");
}

// ---------------------------------------------------------------------------------------------
static void ProbeTests() {
    Out(L"== read-only probes of this PC (nothing is closed or changed)");
    WCHAR wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    g_windowsDir = wd;

    auto plans = ListPowerPlans();
    Out(L"  power plans:");
    for (const auto& p : plans) {
        Out(L"    " + p.first + L"  " + p.second);
    }
    std::wstring active = ActivePlan();
    Out(L"  active: " + active);
    std::wstring label;
    std::wstring ult = ResolveGamePlan(L"ultimate", &label);
    Out(L"  ultimate resolves to: " + ult + L" (" + label + L")");
    Check(!active.empty(), L"active plan is readable");
    Check(!plans.empty(), L"plans are listed");
    Check(SetPlan(active), L"setting the already-active plan again succeeds (API works, nothing changes)");
    Check(ActivePlan() == active, L"active plan unchanged");

    Out(L"  command line, AUMID and creation time of real processes:");
    for (const auto& p : ListProcesses()) {
        std::wstring k = ExeKey(p.exe);
        if (k == L"whatsapp.root" || k == L"flow.launcher" || k == L"files" || k == L"rainmeter") {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
            if (!h) {
                continue;
            }
            std::wstring cl = QueryCommandLine(h);
            std::wstring au = QueryAumid(h);
            CloseHandle(h);
            Out(L"    " + p.exe + L" aumid=[" + au + L"] args=[" + ArgsFromCommandLine(cl) + L"]");
            Check(!cl.empty(), p.exe + L": command line readable");
            if (k == L"whatsapp.root" || k == L"files") {
                Check(!au.empty(), p.exe + L": Store app has an AUMID");
            }
            if (k == L"flow.launcher" || k == L"rainmeter") {
                Check(au.empty(), p.exe + L": desktop app has no AUMID");
            }
        }
    }

    Out(L"  what turning on would list right now (closeOthers on; Claude and the usual keepers kept):");
    Settings s;
    s.closeOthers = true;
    s.closeBackground = true;
    s.keepApps = {L"steam.exe", L"EpicGamesLauncher.exe", L"Battle.net.exe", L"GalaxyClient.exe", L"Discord.exe", L"obs64.exe", L"WindowsTerminal.exe", L"claude.exe"};
    s.keepFolders = {L"\\NVIDIA Corporation\\", L"\\NVIDIA\\", L"\\AMD\\", L"\\Intel\\", L".IntelGraphicsExperience",L"\\Realtek", L"\\Windhawk\\", L"\\WindowsApps\\MicrosoftWindows.", L"\\WindowsApps\\Microsoft.Widgets", L"\\WindowsApps\\Microsoft.DesktopAppInstaller", L"\\EdgeWebView\\"};
    s.keepVendorApps = true;
    s.keepVendors = {L"Acer", L"ULIC", L"Predator", L"Nitro", L"Lenovo", L"Legion", L"Hewlett-Packard", L"HP Inc", L"OMEN", L"Dell", L"Alienware", L"ASUS", L"ROG", L"Armoury", L"Micro-Star", L"MSI", L"Gigabyte", L"Razer", L"Logitech", L"Corsair", L"SteelSeries", L"Intel", L"NVIDIA", L"AMD", L"Advanced Micro Devices", L"Realtek", L"Qualcomm", L"Rivet Networks", L"Killer"};
    std::vector<std::wstring> kept;
    ULONGLONG t0 = GetTickCount64();
    auto groups = CollectGroups(s, &kept);
    Out(L"  collecting took " + std::to_wstring(GetTickCount64() - t0) + L" ms");
    bool claudeListed = false, windhawkListed = false, hasBackground = false, consoleListed = false, steamHelperListed = false;
    for (const auto& g : groups) {
        Out(L"    CLOSE " + g.name + L"  [" + g.path + L"; company=" + FileVersionString(g.path, L"CompanyName") + L"; product=" + FileVersionString(g.path, L"ProductName") + L"]  (" + std::to_wstring(g.pids.size()) + L" proc, " + std::to_wstring(g.windows.size()) + L" win)" +
            (NoteFor(g).empty() ? L"" : L"  <" + NoteFor(g) + L">") + (g.aumid.empty() ? L"" : L"  aumid=" + g.aumid) + (g.args.empty() ? L"" : L"  args=" + g.args));
        claudeListed = claudeListed || ExeKey(g.exe) == L"claude";
        steamHelperListed = steamHelperListed || ExeKey(g.exe) == L"steamwebhelper";
        windhawkListed = windhawkListed || Lower(g.path).find(L"\\windhawk\\") != std::wstring::npos || ExeKey(g.exe) == L"vscodium";
        hasBackground = hasBackground || g.background;
        consoleListed = consoleListed || !IsGuiExe(g.path);
    }
    for (const auto& k2 : kept) {
        Out(L"    keep  " + k2);
    }
    Check(!claudeListed, L"Claude is not in the list (kept, as asked for tests)");
    Check(!windhawkListed, L"Windhawk and its editor (VSCodium) are not in the list");
    Check(!consoleListed, L"no console tool is in the list");
    Check(!steamHelperListed, L"a helper started by a kept app (Steam's web helper) is not in the list");
    bool acerListed = false;
    for (const auto& g : groups) {
        acerListed = acerListed || VendorMatches(FileVersionString(g.path, L"CompanyName") + L" | " + g.path, {L"Acer"});
    }
    Check(!acerListed, L"Acer tools (PC maker utilities) are not in the list");
    s.keepVendorApps = false;
    bool acerBack = false;
    for (const auto& g : CollectGroups(s, nullptr)) {
        acerBack = acerBack || VendorMatches(FileVersionString(g.path, L"CompanyName") + L" | " + g.path, {L"Acer"});
    }
    Check(acerBack, L"with the setting off the Acer tools are listed again");
    Check(hasBackground, L"background and tray apps are listed too, not only taskbar apps");
    for (const auto& g : groups) {
        Check(!g.windows.empty() || g.windowless, g.name + L": windows found, or marked as windowless");
    }
}

// ---------------------------------------------------------------------------------------------
static bool Running(const wchar_t* name) {
    return GetModuleHandleW(nullptr) && [&] {
        for (const auto& p : ListProcesses()) {
            if (ExeKey(p.exe) == ExeKey(name)) {
                return true;
            }
        }
        return false;
    }();
}

// Cold .NET starts take a while; wait for the condition instead of sleeping a fixed time.
static bool WaitFor(const std::function<bool()>& cond, int ms = 12000) {
    for (int waited = 0; waited < ms; waited += 200) {
        if (cond()) {
            return true;
        }
        Sleep(200);
    }
    return cond();
}

static DWORD PriorityOf(const wchar_t* name) {
    for (const auto& p : ListProcesses()) {
        if (ExeKey(p.exe) == ExeKey(name)) {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
            DWORD c = h ? GetPriorityClass(h) : 0;
            if (h) {
                CloseHandle(h);
            }
            return c;
        }
    }
    return 0;
}

static void KillFakes() {
    for (const auto& p : ListProcesses()) {
        if (_wcsnicmp(p.exe.c_str(), L"GmFake", 6) == 0) {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid);
            if (h) {
                TerminateProcess(h, 1);
                CloseHandle(h);
            }
        }
    }
    Sleep(600);
}

static std::wstring FakesRunning() {
    std::set<std::wstring> names;
    for (const auto& p : ListProcesses()) {
        if (_wcsnicmp(p.exe.c_str(), L"GmFake", 6) == 0) {
            names.insert(ExeKey(p.exe));
        }
    }
    std::wstring o;
    for (const auto& n : names) {
        o += n + L" ";
    }
    return o.empty() ? L"(none)" : o;
}

static void Launch(const std::wstring& exe, const std::wstring& args) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

static std::wstring ReadAll(const std::wstring& path) {
    std::wstring t;
    ReadFileText(path, &t);
    return t;
}

static void LiveTests(const std::wstring& testsDir) {
    Out(L"== live tests against fake apps only");
    std::wstring bin = testsDir + L"\\bin";
    std::wstring games = testsDir + L"\\Games";
    if (GetFileAttributesW((bin + L"\\GmFakeWin.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        Out(L"  fake apps are not built; run tests\\build-fakes.ps1 first");
        g_fail++;
        return;
    }
    WCHAR wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    g_windowsDir = wd;

    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_storage = std::wstring(tmp) + L"gmt-test-" + std::to_wstring(GetCurrentProcessId());
    std::wstring snap = SnapshotPath();  // creates the storage folder, per-user file name

    auto base = [&]() {
        Settings s;
        s.confirm = false;
        s.notify = false;
        s.closeOthers = false;  // never discover real apps in a live test
        s.plan = L"none";
        s.timeoutSec = 6;
        s.closeApps = {L"GmFakeWin", L"GmFakeTray", L"GmFakeSlow", L"GmFakeArgs", L"GmFakeDeaf"};
        s.gameFolders = {games};
        return s;
    };
    auto guard = [&](const Settings& s) {
        bool safe = !s.closeOthers;
        for (const auto& a : s.closeApps) {
            safe = safe && _wcsnicmp(a.c_str(), L"GmFake", 6) == 0;
        }
        if (!safe) {
            Out(L"  ABORT: a live test would reach a real app");
            ExitProcess(3);
        }
    };
    auto startFakes = [&]() {
        KillFakes();
        Launch(bin + L"\\GmFakeWin.exe", L"");
        Launch(bin + L"\\GmFakeTray.exe", L"--tray");
        Launch(bin + L"\\GmFakeDeaf.exe", L"--deaf");
        Launch(bin + L"\\GmFakeArgs.exe", L"--profile \"My Profile\" -x");
        Launch(bin + L"\\GmFakeSlow.exe", L"--slow 2 -osint -url https://example.test/oauth?token=SECRET123");
        Launch(games + L"\\GmFakeGame.exe", L"");
        Sleep(2500);
        for (const auto& p : ListProcesses()) {
            if (ExeKey(p.exe) == L"gmfakegame") {
                HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, p.pid);
                if (h) {
                    SetPriorityClass(h, BELOW_NORMAL_PRIORITY_CLASS);
                    CloseHandle(h);
                }
            }
        }
    };

    // --- L1: round trip
    Out(L"-- L1 turn on, then off");
    g_settings = base();
    guard(g_settings);
    startFakes();
    Out(L"  before: " + FakesRunning());
    Check(Running(L"GmFakeWin") && Running(L"GmFakeTray") && Running(L"GmFakeDeaf") && Running(L"GmFakeArgs") && Running(L"GmFakeSlow") && Running(L"GmFakeGame"),
          L"precondition: all six fakes started");
    Toggle();
    Out(L"  after on: " + FakesRunning());
    for (const auto& w : ListTopLevelWindows()) {  // what a surviving fake is showing
        WCHAR title[200] = {};
        GetWindowTextW(w.hwnd, title, 200);
        for (const auto& p : ListProcesses()) {
            if (p.pid == w.pid && _wcsnicmp(p.exe.c_str(), L"GmFake", 6) == 0 && (w.taskbar || IsWindowVisible(w.hwnd))) {
                Out(L"    survivor window: " + p.exe + L" title=[" + title + L"] visible=" + std::to_wstring(IsWindowVisible(w.hwnd)));
            }
        }
    }
    Check(!Running(L"GmFakeWin"), L"a window app was closed");
    Check(Running(L"GmFakeTray"), L"without permission to end it, a tray app with only hidden windows is left untouched (its tray icon and hotkey keep working)");
    Check(!Running(L"GmFakeArgs"), L"an app with arguments was closed");
    Check(!Running(L"GmFakeSlow"), L"an app that needs 2 s to close was waited for");
    Check(Running(L"GmFakeDeaf"), L"an app that ignores the close is left running, not killed");
    Check(PriorityOf(L"GmFakeGame") == HIGH_PRIORITY_CLASS, L"the running game got High priority");
    Check(Running(L"GmFakeGame"), L"the game itself was not closed");
    std::wstring text = ReadAll(snap);
    Check(!text.empty(), L"snapshot written");
    Check(text.find(L"GmFakeDeaf") == std::wstring::npos, L"the app that did not close is not in the snapshot (nothing to reopen)");
    Check(text.find(L"SECRET123") == std::wstring::npos, L"a link a browser was started with is not stored");
    Check(text.find(L"My Profile") != std::wstring::npos, L"normal arguments are stored");
    Toggle();
    WaitFor([] { return Running(L"GmFakeWin") && Running(L"GmFakeTray") && Running(L"GmFakeArgs") && Running(L"GmFakeSlow"); });
    Out(L"  after off: " + FakesRunning());
    Check(Running(L"GmFakeWin") && Running(L"GmFakeTray") && Running(L"GmFakeArgs") && Running(L"GmFakeSlow"), L"the apps that were closed came back and the tray app is still there");
    Check(PriorityOf(L"GmFakeGame") == BELOW_NORMAL_PRIORITY_CLASS, L"the game priority is back to what it was");
    Check(GetFileAttributesW(snap.c_str()) == INVALID_FILE_ATTRIBUTES, L"snapshot deleted after the restore");
    bool argsBack = false;
    for (const auto& p : ListProcesses()) {
        if (ExeKey(p.exe) == L"gmfakeargs") {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
            argsBack = h && QueryCommandLine(h).find(L"--profile \"My Profile\" -x") != std::wstring::npos;
            if (h) {
                CloseHandle(h);
            }
        }
    }
    Check(argsBack, L"the app was reopened with its original arguments");

    // --- L2: idempotency of the toggle and an app reopened by hand
    Out(L"-- L2 an app reopened by hand during the game is not started twice");
    startFakes();
    Toggle();
    Launch(bin + L"\\GmFakeWin.exe", L"");
    Sleep(1500);
    Toggle();
    Sleep(4000);  // a wrongly started second copy would show up within this time
    int wins = 0;
    for (const auto& p : ListProcesses()) {
        wins += ExeKey(p.exe) == L"gmfakewin";
    }
    Check(wins == 1, L"one GmFakeWin after the restore, not two (found " + std::to_wstring(wins) + L")");

    // --- L3: force close list
    Out(L"-- L3 force close");
    startFakes();
    g_settings = base();
    g_settings.timeoutSec = 2;
    g_settings.forceApps = {L"GmFakeDeaf.exe"};
    guard(g_settings);
    Toggle();
    Check(!Running(L"GmFakeDeaf"), L"a listed force-close app that ignores the close is ended after the wait");
    Toggle();
    WaitFor([] { return Running(L"GmFakeDeaf"); });
    Check(Running(L"GmFakeDeaf"), L"and it is reopened on the way back");

    // --- L4: corrupt and half-written snapshot
    Out(L"-- L4 corrupt snapshot");
    g_settings = base();
    startFakes();
    CreateDirectoryW(g_storage.c_str(), nullptr);
    {
        HANDLE h = CreateFileW(snap.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        const char junk[] = "GMT1\nstarted\t1338\napp\tZen\tC:\\x";
        DWORD w;
        WriteFile(h, junk, sizeof(junk) - 1, &w, nullptr);
        CloseHandle(h);
    }
    Out(L"  before: " + FakesRunning());
    Toggle();
    Out(L"  after: " + FakesRunning());
    Check(GetFileAttributesW(snap.c_str()) == INVALID_FILE_ATTRIBUTES, L"an unreadable snapshot is discarded");
    Check(Running(L"GmFakeWin") && Running(L"GmFakeArgs"), L"and that press did not turn anything on");

    // --- L5: leftover from before a restart is recovered quietly
    Out(L"-- L5 recovery of a leftover snapshot");
    KillFakes();
    {
        Snapshot sn;
        sn.startedAt = NowFileTime() - 5ULL * 24 * 3600 * 10000000ULL;  // five days ago
        sn.apps.push_back({L"GmFakeWin", bin + L"\\GmFakeWin.exe", L"", L""});
        sn.apps.push_back({L"GmFakeArgs", bin + L"\\GmFakeArgs.exe", L"--profile P", L""});
        Check(WriteSnapshot(sn), L"leftover snapshot written");
    }
    Check(SnapshotIsLeftOver(), L"a snapshot from before the last restart is recognised");
    Recover();
    WaitFor([] { return Running(L"GmFakeWin") && Running(L"GmFakeArgs"); });
    Check(Running(L"GmFakeWin") && Running(L"GmFakeArgs"), L"recovery reopened the apps");
    Check(GetFileAttributesW(snap.c_str()) == INVALID_FILE_ATTRIBUTES, L"and removed the snapshot");
    {
        Snapshot sn;
        sn.startedAt = NowFileTime();
        sn.apps.push_back({L"GmFakeWin", bin + L"\\GmFakeWin.exe", L"", L""});
        WriteSnapshot(sn);
        Check(!SnapshotIsLeftOver(), L"a snapshot from this session is not treated as leftover");
        sn.logon = CurrentLogonId();
        WriteSnapshot(sn);
        Check(!sn.logon.empty() && !SnapshotIsLeftOver(), L"a snapshot from this sign-in is not leftover");
        sn.logon = L"0000000000000001";  // another sign-in, even though the tick count never reset
        WriteSnapshot(sn);
        Check(SnapshotIsLeftOver(), L"a snapshot from an earlier sign-in is leftover (Fast Startup)");
        Snapshot rt;
        Check(LoadSnapshot(&rt) == LoadResult::Ok && rt.logon == sn.logon, L"the sign-in id survives the file");
        DeleteSnapshot();
        Check(SnapshotPath().find(UserSidText()) != std::wstring::npos, L"the snapshot file is named after the user's SID");
    }

    // --- L5b: unloading while on puts the machine-wide settings back
    Out(L"-- L5b unload restores the power plan");
    {
        std::wstring active = ActivePlan();
        std::wstring other;
        for (const auto& p : ListPowerPlans()) {
            if (p.first != active) { other = p.first; break; }
        }
        if (other.empty() || active.empty()) {
            Out(L"  (only one power plan here, skipped)");
        } else {
            Snapshot sn;
            sn.startedAt = NowFileTime();
            sn.logon = CurrentLogonId();
            sn.plan = active;
            sn.apps.push_back({L"GmFakeWin", bin + L"\\GmFakeWin.exe", L"", L""});
            WriteSnapshot(sn);
            SetPlan(other);
            RestoreOnUnload();
            Check(ActivePlan() == active, L"the power plan is back after unload");
            Snapshot after;
            Check(LoadSnapshot(&after) == LoadResult::Ok && after.plan.empty() && after.apps.size() == 1,
                  L"the snapshot keeps the apps but no longer the plan");
            DeleteSnapshot();
        }
    }

    // --- L6: failed relaunch keeps the snapshot for a retry
    Out(L"-- L6 a failed relaunch keeps only that app for the next press");
    KillFakes();
    {
        Snapshot sn;
        sn.startedAt = NowFileTime();
        sn.apps.push_back({L"GmFakeWin", bin + L"\\GmFakeWin.exe", L"", L""});
        sn.apps.push_back({L"Gone", bin + L"\\DoesNotExist.exe", L"", L""});
        WriteSnapshot(sn);
    }
    g_settings = base();
    Toggle();
    WaitFor([] { return Running(L"GmFakeWin"); });
    Snapshot left;
    Check(LoadSnapshot(&left) == LoadResult::Ok && left.apps.size() == 1 && left.apps[0].name == L"Gone", L"only the app that failed stays in the snapshot");
    Check(Running(L"GmFakeWin"), L"the app that could be reopened was reopened");
    DeleteSnapshot();

    KillFakes();
    RemoveDirectoryW(g_storage.c_str());
}

// ---------------------------------------------------------------------------------------------
// Dialog tests: a helper thread drives the real dialog window like a user would.

static HWND WaitForDialog() {
    for (int i = 0; i < 100; i++) {
        HWND h = FindWindowW(kDlgClass, nullptr);
        if (h && IsWindowVisible(h)) {
            return h;
        }
        Sleep(50);
    }
    return nullptr;
}

typedef void (*DialogScript)(HWND);
static DWORD WINAPI AutomatorThread(LPVOID p) {
    HWND h = WaitForDialog();
    if (h) {
        Sleep(300);
        ((DialogScript)p)(h);
    }
    return 0;
}

static ChecklistResult RunDialog(const ChecklistSpec& spec, DialogScript script) {
    HANDLE t = CreateThread(nullptr, 0, AutomatorThread, (LPVOID)script, 0, nullptr);
    ChecklistResult r = ShowChecklist(spec);
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    return r;
}

static std::wstring TextOf(HWND h) {
    WCHAR b[300] = {};
    GetWindowTextW(h, b, 300);
    return b;
}

static void Click(HWND dlg, int id) {
    SendMessageW(GetDlgItem(dlg, id), BM_CLICK, 0, 0);
    Sleep(80);
}

static ChecklistSpec SampleSpec(size_t n, bool expanded) {
    static const wchar_t* const names[] = {L"Discord Canary", L"Google Chrome", L"Notion", L"OneNote", L"Rainmeter", L"Spotify", L"Wallpaper Engine", L"Samsung Notes"};
    static const wchar_t* const notes[] = {L"", L"", L"", L"", L"background", L"", L"background", L"ends process"};
    ChecklistSpec s = OnChecklist({}, true, false, L"Ultimate Performance", 1);
    for (size_t i = 0; i < n; i++) {
        if (i < 8) {
            s.items.push_back({names[i], notes[i]});
        } else {
            s.items.push_back({L"Sample app number " + std::to_wstring(i + 1), L""});
        }
    }
    s.startExpanded = expanded;
    return s;
}

static void DialogTests() {
    Out(L"== checklist dialog, driven like a user");
    g_settings.confirm = true;

    static ChecklistResult seen;
    static bool visBefore, visAfter, summaryOk, tristateOk, allOnOk, allOffOk;
    static std::wstring firstLabel, longLabel;
    static bool defaultOk;

    // 1) expand, untick one, select all, clear all, pick two, OK
    auto script1 = [](HWND h) {
        visBefore = IsWindowVisible(GetDlgItem(h, kIdItem0));
        // Focus belongs to the dialog's thread, not ours, and arrives a moment after the window shows.
        defaultOk = WaitFor([h] {
            GUITHREADINFO gti = {sizeof(gti)};
            GetGUIThreadInfo(GetWindowThreadProcessId(h, nullptr), &gti);
            return gti.hwndFocus == GetDlgItem(h, IDOK);
        }, 3000);
        Click(h, kIdAdv);
        visAfter = IsWindowVisible(GetDlgItem(h, kIdItem0));
        firstLabel = TextOf(GetDlgItem(h, kIdItem0));
        HWND item2 = GetDlgItem(h, kIdItem0 + 2);
        std::wstring beforeClick = TextOf(g_dlg->summaryWnd);
        LRESULT stateBefore = SendMessageW(item2, BM_GETCHECK, 0, 0);
        Click(h, kIdItem0 + 2);
        summaryOk = TextOf(g_dlg->summaryWnd) == L"Closes 7 of 8 apps";
        if (!summaryOk) {
            Out(L"  DEBUG summary before=[" + beforeClick + L"] after=[" + TextOf(g_dlg->summaryWnd) + L"] item2 state before=" + std::to_wstring((long long)stateBefore) +
                L" after=" + std::to_wstring((long long)SendMessageW(item2, BM_GETCHECK, 0, 0)) + L" visible=" + std::to_wstring(IsWindowVisible(item2)) +
                L" enabled=" + std::to_wstring(IsWindowEnabled(item2)) + L" expanded=" + std::to_wstring(g_dlg->expanded));
        }
        tristateOk = SendMessageW(GetDlgItem(h, kIdAll), BM_GETCHECK, 0, 0) == BST_INDETERMINATE;
        Click(h, kIdAll);
        allOnOk = TextOf(g_dlg->summaryWnd) == L"Closes 8 of 8 apps";
        Click(h, kIdAll);
        allOffOk = TextOf(g_dlg->summaryWnd) == L"Closes 0 of 8 apps";
        Click(h, kIdItem0 + 0);
        Click(h, kIdItem0 + 4);
        Click(h, IDOK);
    };
    seen = RunDialog(SampleSpec(8, false), script1);
    Check(seen.ok, L"OK returns ok");
    Check(defaultOk, L"focus starts on the default button (Enter confirms)");
    Check(!visBefore && visAfter, L"the list is hidden until Advanced options is opened");
    Check(firstLabel == L"Discord Canary", L"an app is shown by its name");
    Check(summaryOk, L"unticking one app updates the summary line");
    Check(tristateOk, L"Select all shows a partial state");
    Check(allOnOk && allOffOk, L"Select all ticks and clears every app");
    bool pick = seen.checked.size() == 8;
    for (size_t i = 0; pick && i < 8; i++) {
        pick = seen.checked[i] == (i == 0 || i == 4);
    }
    Check(pick, L"the result lists exactly the two apps that were ticked");

    // 2) Esc cancels
    auto escScript = [](HWND h) { PostMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0); };
    Check(!RunDialog(SampleSpec(5, false), escScript).ok, L"Esc cancels");

    // 3) Enter confirms with everything ticked
    auto enterScript = [](HWND h) { PostMessageW(h, WM_KEYDOWN, VK_RETURN, 0); };
    ChecklistResult e = RunDialog(SampleSpec(5, false), enterScript);
    Check(e.ok && std::count(e.checked.begin(), e.checked.end(), true) == 5, L"Enter confirms with everything ticked");

    // 4) the window's close button cancels
    auto closeScript = [](HWND h) { PostMessageW(h, WM_CLOSE, 0, 0); };
    Check(!RunDialog(SampleSpec(5, false), closeScript).ok, L"the close button cancels");

    // 5) Cancel button
    auto cancelScript = [](HWND h) { Click(h, IDCANCEL); };
    Check(!RunDialog(SampleSpec(5, false), cancelScript).ok, L"the Cancel button cancels");

    // 6) Mod stopped while the dialog is open: the dialog closes without confirming and the stop is passed on
    static DWORD dialogThread;
    dialogThread = GetCurrentThreadId();
    auto stopScript2 = [](HWND) { PostThreadMessageW(dialogThread, WM_APP_STOP, 0, 0); };
    ChecklistResult stopped = RunDialog(SampleSpec(3, false), stopScript2);
    MSG msg;
    bool repost = PeekMessageW(&msg, nullptr, WM_APP_STOP, WM_APP_STOP, PM_REMOVE);
    Check(!stopped.ok && repost, L"stopping the mod closes the dialog without confirming and keeps the stop request");

    // 7) empty list
    static bool emptyOk;
    auto emptyScript = [](HWND h) {
        emptyOk = TextOf(g_dlg->summaryWnd) == L"Closes nothing" && !IsWindowVisible(GetDlgItem(h, kIdAdv));
        Click(h, IDOK);
    };
    ChecklistResult empty = RunDialog(SampleSpec(0, false), emptyScript);
    Check(empty.ok && emptyOk, L"with nothing to close the summary says so and there is no list");

    // 8) many apps: three columns, the window fits the screen, long names end in an ellipsis
    static bool fits, threeCols, ellipsis;
    auto manyScript = [](HWND h) {
        RECT w, a;
        GetWindowRect(h, &w);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        a = mi.rcWork;
        fits = w.left >= a.left && w.top >= a.top && w.right <= a.right && w.bottom <= a.bottom;
        RECT r0, r1, r2;
        GetWindowRect(GetDlgItem(h, kIdItem0 + 0), &r0);
        GetWindowRect(GetDlgItem(h, kIdItem0 + 1), &r1);
        GetWindowRect(GetDlgItem(h, kIdItem0 + 2), &r2);
        threeCols = r0.top == r1.top && r1.top == r2.top;
        ellipsis = TextOf(GetDlgItem(h, kIdItem0 + 30)).back() == L'…';
        Click(h, IDOK);
    };
    ChecklistSpec many = SampleSpec(40, true);
    many.items[30].text = L"A very long application name that can never fit in one column of the list";
    ChecklistResult manyRes = RunDialog(many, manyScript);
    Check(manyRes.ok && manyRes.checked.size() == 40, L"40 apps work");
    Check(fits, L"the window stays inside the screen with 40 apps");
    Check(threeCols, L"many apps use three columns");
    Check(ellipsis, L"a name that does not fit ends with an ellipsis");

    // 9) long names: a plain note is dropped before the name is cut; a warning note stays
    static std::wstring plainShown, warnShown;
    auto noteScript = [](HWND h) {
        plainShown = TextOf(GetDlgItem(h, kIdItem0 + 0));
        warnShown = TextOf(GetDlgItem(h, kIdItem0 + 1));
        Click(h, IDOK);
    };
    ChecklistSpec notes = SampleSpec(2, true);
    notes.items[0] = {L"Rainmeter desktop customization tool", L"background", false};
    notes.items[1] = {L"Samsung Find Client With A Very Long Product Name Indeed", L"ends process", true};
    RunDialog(notes, noteScript);
    Check(plainShown.find(L"(background)") == std::wstring::npos && plainShown.find(L"Rainmeter") == 0, L"a plain note is dropped before a name is cut");
    Check(warnShown.find(L"(ends process)") != std::wstring::npos && warnShown.find(L"…") != std::wstring::npos, L"a warning note stays and the name is cut instead");
}

// Shows the real dialog with made-up app names, for screenshots. Nothing is closed.
static void ShowSampleDialog(bool on, bool expanded) {
    g_settings.confirm = true;
    ChecklistSpec spec = SampleSpec(8, expanded);
    if (!on) {
        std::vector<ChecklistItem> items;
        for (const auto& it : spec.items) {
            items.push_back({it.text, L""});
        }
        spec = OffChecklist(items, true);
        spec.startExpanded = expanded;
    }
    ChecklistResult r = ShowChecklist(spec);
    Out(r.ok ? L"dialog: OK" : L"dialog: Cancel");
}

static void ToggleWith(DialogScript script) {
    HANDLE t = CreateThread(nullptr, 0, AutomatorThread, (LPVOID)script, 0, nullptr);
    Toggle();
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
}

static void WindowlessLive(const std::wstring& testsDir) {
    Out(L"-- L7 an app with no window at all is ended only when it is ticked in the checklist");
    std::wstring bin = testsDir + L"\\bin";
    if (GetFileAttributesW((bin + L"\\GmFakeNoWin.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        Out(L"  GmFakeNoWin.exe is not built; run tests\\build-fakes.ps1 first");
        g_fail++;
        return;
    }
    WCHAR wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    g_windowsDir = wd;
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_storage = std::wstring(tmp) + L"gmt-test-nowin-" + std::to_wstring(GetCurrentProcessId());

    Settings s;
    s.notify = false;
    s.closeOthers = false;  // never discover real apps in a live test
    s.plan = L"none";
    s.timeoutSec = 3;
    s.closeApps = {L"GmFakeNoWin"};
    auto start = [&]() {
        KillFakes();
        Launch(bin + L"\\GmFakeNoWin.exe", L"");
        WaitFor([] { return Running(L"GmFakeNoWin"); });
    };
    auto clickOk = [](HWND h) { Click(h, IDOK); };

    start();
    s.confirm = false;
    g_settings = s;
    Toggle();
    Check(Running(L"GmFakeNoWin"), L"with the checklist off a window-less app is only asked to close, so it keeps running");
    Toggle();  // off (nothing to reopen)

    start();
    s.confirm = true;
    g_settings = s;
    ToggleWith(clickOk);
    Check(!Running(L"GmFakeNoWin"), L"left ticked in the checklist, the window-less app is ended");
    ToggleWith(clickOk);  // off: reopens it
    WaitFor([] { return Running(L"GmFakeNoWin"); });
    Check(Running(L"GmFakeNoWin"), L"and it is reopened on the way back");

    start();
    auto untickThenOk = [](HWND h) {
        Click(h, kIdAdv);
        Click(h, kIdItem0 + 0);
        Click(h, IDOK);
    };
    ToggleWith(untickThenOk);
    Check(Running(L"GmFakeNoWin"), L"unticked in the checklist, the window-less app is left alone");
    ToggleWith(clickOk);  // off
    KillFakes();
    RemoveDirectoryW(g_storage.c_str());
}

// An app that hides to the tray when asked to close (WhatsApp, Phone Link): its window disappears
// but the process lives on. It must not be silently forgotten: end it (if ticked) and put it back.
static void HideToTrayLive(const std::wstring& testsDir) {
    Out(L"-- L8 an app that only hides to the tray when asked to close");
    std::wstring bin = testsDir + L"\\bin";
    if (GetFileAttributesW((bin + L"\\GmFakeHide.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        Out(L"  GmFakeHide.exe is not built; run tests\\build-fakes.ps1 first");
        g_fail++;
        return;
    }
    WCHAR wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    g_windowsDir = wd;
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_storage = std::wstring(tmp) + L"gmt-test-hide-" + std::to_wstring(GetCurrentProcessId());
    std::wstring snap = SnapshotPath();

    Settings s;
    s.notify = false;
    s.closeOthers = false;  // never discover real apps in a live test
    s.plan = L"none";
    s.timeoutSec = 2;
    s.closeApps = {L"GmFakeHide"};
    auto start = [&]() {
        KillFakes();
        Launch(bin + L"\\GmFakeHide.exe", L"--hide");
        WaitFor([] { return Running(L"GmFakeHide"); });
        Sleep(1200);
    };
    auto clickOk = [](HWND h) { Click(h, IDOK); };

    // with the checklist off nothing is ended: it stays running, hidden
    start();
    s.confirm = false;
    g_settings = s;
    Out(L"  before: " + FakesRunning());
    {
        auto groups = CollectGroups(s, nullptr);
        for (const auto& g : groups) {
            Out(L"  group " + g.name + L": " + std::to_wstring(g.pids.size()) + L" proc, " + std::to_wstring(g.windows.size()) + L" win, windowless=" + std::to_wstring(g.windowless));
        }
    }
    Toggle();
    Out(L"  after on: " + FakesRunning());
    Check(Running(L"GmFakeHide"), L"with the checklist off a hide-to-tray app is not ended");
    Toggle();

    // ticked: ended after the extra wait, then reopened on the way back
    start();
    s.confirm = true;
    g_settings = s;
    ToggleWith(clickOk);
    Check(!Running(L"GmFakeHide"), L"ticked in the checklist, an app that only hides is ended");
    std::wstring saved = ReadAll(snap);
    Check(saved.find(L"GmFakeHide") != std::wstring::npos, L"and it is in the snapshot, so turning off can put it back");
    static std::wstring offItem;
    auto offScript = [](HWND h) {
        Click(h, kIdAdv);
        offItem = TextOf(GetDlgItem(h, kIdItem0 + 0));
        Click(h, IDOK);
    };
    ToggleWith(offScript);
    WaitFor([] { return Running(L"GmFakeHide"); });
    Check(offItem.find(L"GmFakeHide") != std::wstring::npos, L"the turn-off checklist lists it");
    Check(Running(L"GmFakeHide"), L"and it is reopened");

    // unticked: left alone
    start();
    auto untickThenOk = [](HWND h) {
        Click(h, kIdAdv);
        Click(h, kIdItem0 + 0);
        Click(h, IDOK);
    };
    ToggleWith(untickThenOk);
    Check(Running(L"GmFakeHide"), L"unticked in the checklist, it is left alone");
    ToggleWith(clickOk);
    KillFakes();
    RemoveDirectoryW(g_storage.c_str());
}

// How many top-level windows (hidden ones too) a process owns: a tray app's hotkey and tray icon live in them.
static size_t WindowCountOf(const wchar_t* exeName) {
    size_t n = 0;
    std::set<DWORD> pids;
    for (const auto& p : ListProcesses()) {
        if (ExeKey(p.exe) == ExeKey(exeName)) {
            pids.insert(p.pid);
        }
    }
    for (const auto& w : ListTopLevelWindows()) {
        n += pids.count(w.pid);
    }
    return n;
}

static void TrayAppLive(const std::wstring& testsDir) {
    Out(L"-- L9 tray apps whose only windows are hidden (hotkey and tray helper windows)");
    std::wstring bin = testsDir + L"\\bin";
    WCHAR wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    g_windowsDir = wd;
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_storage = std::wstring(tmp) + L"gmt-test-tray-" + std::to_wstring(GetCurrentProcessId());

    Settings s;
    s.notify = false;
    s.closeOthers = false;  // never discover real apps in a live test
    s.plan = L"none";
    s.timeoutSec = 2;
    s.closeApps = {L"GmFakeTray", L"GmFakeDeaf"};
    auto start = [&]() {
        KillFakes();
        Launch(bin + L"\\GmFakeTray.exe", L"--tray");
        Launch(bin + L"\\GmFakeDeaf.exe", L"--deaf");
        WaitFor([] { return Running(L"GmFakeTray") && Running(L"GmFakeDeaf"); });
        Sleep(1500);
    };
    auto clickOk = [](HWND h) { Click(h, IDOK); };

    // no permission to end them: their hidden windows must not be touched at all
    start();
    size_t trayBefore = WindowCountOf(L"GmFakeTray");
    size_t deafBefore = WindowCountOf(L"GmFakeDeaf");
    s.confirm = false;
    g_settings = s;
    Toggle();
    Check(Running(L"GmFakeTray") && Running(L"GmFakeDeaf"), L"without permission both tray apps keep running");
    Check(WindowCountOf(L"GmFakeTray") == trayBefore && WindowCountOf(L"GmFakeDeaf") == deafBefore,
          L"and keep every hidden window (" + std::to_wstring(trayBefore) + L" and " + std::to_wstring(deafBefore) + L"), so tray icon and hotkey still work");
    Toggle();  // off

    // permission given (ticked in the checklist): they are closed or ended, and restored on the way back
    start();
    s.confirm = true;
    g_settings = s;
    ToggleWith(clickOk);
    Check(!Running(L"GmFakeTray"), L"ticked in the checklist, the tray app is closed");
    Check(!Running(L"GmFakeDeaf"), L"and an app that ignores the close is ended, not left alive without its windows");
    ToggleWith(clickOk);
    WaitFor([] { return Running(L"GmFakeTray") && Running(L"GmFakeDeaf"); });
    Check(Running(L"GmFakeTray") && Running(L"GmFakeDeaf"), L"both come back when Game Mode is turned off");
    KillFakes();
    RemoveDirectoryW(g_storage.c_str());
}

int wmain(int argc, wchar_t** argv) {
    std::wstring mode = argc > 1 ? argv[1] : L"unit";
    if (mode == L"dialog-on" || mode == L"dialog-on-open" || mode == L"dialog-off" || mode == L"dialog-off-open") {
        ShowSampleDialog(mode.rfind(L"dialog-on", 0) == 0, mode.size() > 4 && mode.substr(mode.size() - 5) == L"-open");
        return 0;
    }
    if (mode == L"dialog") {
        DialogTests();
        Out(L"");
        Out(L"passed " + std::to_wstring(g_pass) + L", failed " + std::to_wstring(g_fail));
        return g_fail ? 1 : 0;
    }
    if (mode == L"unit") {
        UnitTests();
    } else if (mode == L"probe") {
        ProbeTests();
    } else if (mode == L"live" && argc > 2) {
        LiveTests(argv[2]);
        WindowlessLive(argv[2]);
        HideToTrayLive(argv[2]);
        TrayAppLive(argv[2]);
    } else {
        Out(L"usage: test.exe unit | probe | live <tests dir>");
        return 2;
    }
    Out(L"");
    Out(L"passed " + std::to_wstring(g_pass) + L", failed " + std::to_wstring(g_fail));
    return g_fail ? 1 : 0;
}
