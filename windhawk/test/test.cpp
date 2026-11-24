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

static std::wstring TestFont();
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
    ULONGLONG t0 = GetTickCount64();
    auto groups = CollectGroups(s);
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
    for (const auto& g : CollectGroups(s)) {
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
// Window tests: a helper thread drives the real window with keys, like a user would.

static HWND WaitForDialog() {
    for (int i = 0; i < 100; i++) {
        HWND h = FindWindowW(L"GameModeToggleFlow", nullptr);
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
        Sleep(900);   // the opening animation
        ((DialogScript)p)(h);
    }
    return 0;
}

static void Key(HWND h, WPARAM vk) {
    PostMessageW(h, WM_KEYDOWN, vk, 0);
    Sleep(140);
}

// The old checklist had buttons to click; the new window is driven by keys. OK = Enter, Cancel = Esc.
static void Click(HWND h, int id) {
    Key(h, id == IDCANCEL ? VK_ESCAPE : VK_RETURN);
}

static ChecklistResult RunDialog(const ChecklistSpec& spec, DialogScript script) {
    HANDLE t = CreateThread(nullptr, 0, AutomatorThread, (LPVOID)script, 0, nullptr);
    ChecklistResult r = flow::Review(spec);
    WaitForSingleObject(t, 5000);
    CloseHandle(t);
    flow::Close();   // an OK leaves the window open in its progress view
    return r;
}

static ChecklistSpec SampleSpec(size_t n) {
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
    return s;
}

static void DialogTests() {
    Out(L"== the Game Mode window, driven like a user");
    g_settings.confirm = true;
    g_settings.fontName = TestFont();
    g_settings.theme = L"oled-purple";

    // 1) Enter confirms with everything ticked
    auto enterScript = [](HWND h) { Key(h, VK_RETURN); };
    ChecklistResult e = RunDialog(SampleSpec(5), enterScript);
    Check(e.ok && std::count(e.checked.begin(), e.checked.end(), true) == 5, L"Enter confirms with everything ticked");

    // 2) Esc cancels
    auto escScript = [](HWND h) { Key(h, VK_ESCAPE); };
    Check(!RunDialog(SampleSpec(5), escScript).ok, L"Esc cancels");

    // 3) Tab to the first app, Space unticks it, Enter confirms
    auto untickScript = [](HWND h) { Key(h, VK_TAB); Key(h, VK_SPACE); Key(h, VK_RETURN); };
    ChecklistResult u = RunDialog(SampleSpec(4), untickScript);
    Check(u.ok && u.checked.size() == 4 && !u.checked[0] && u.checked[1] && u.checked[2] && u.checked[3], L"Tab and Space untick an app, Enter confirms the rest");

    // 4) everything unticked: Enter still confirms (the power plan and priorities still apply), and nothing is ticked
    auto noneScript = [](HWND h) {
        Key(h, VK_TAB);   // first app
        Key(h, VK_SPACE);
        Key(h, VK_TAB);
        Key(h, VK_SPACE);
        Key(h, VK_RETURN);
    };
    ChecklistResult none = RunDialog(SampleSpec(2), noneScript);
    Check(none.ok && none.checked.size() == 2 && !none.checked[0] && !none.checked[1], L"with every app unticked Enter still confirms, and no app is picked");

    // 5) the close request cancels
    auto closeScript = [](HWND h) { PostMessageW(h, WM_CLOSE, 0, 0); };
    Check(!RunDialog(SampleSpec(3), closeScript).ok, L"closing the window cancels");

    // 6) the mod is stopped while the window is open: it closes without confirming and the stop is passed on
    static DWORD dialogThread;
    dialogThread = GetCurrentThreadId();
    auto stopScript = [](HWND) { PostThreadMessageW(dialogThread, WM_APP_STOP, 0, 0); };
    ChecklistResult stopped = RunDialog(SampleSpec(3), stopScript);
    MSG msg;
    bool repost = PeekMessageW(&msg, nullptr, WM_APP_STOP, WM_APP_STOP, PM_REMOVE);
    Check(!stopped.ok && repost, L"stopping the mod closes the window without confirming and keeps the stop request");

    // 7) an empty list can still be confirmed (a run that only changes the power plan)
    ChecklistResult empty = RunDialog(SampleSpec(0), enterScript);
    Check(empty.ok && empty.checked.empty(), L"with no apps the main button still confirms");

    // 8) centred on the monitor, and the size follows the scale setting
    static bool centred, sizeOk;
    static int cdx, cdy;
    auto layoutScript = [](HWND h) {
        RECT r;
        GetWindowRect(h, &r);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        cdx = (r.left + r.right) / 2 - (mi.rcWork.left + mi.rcWork.right) / 2;
        cdy = (r.top + r.bottom) / 2 - (mi.rcWork.top + mi.rcWork.bottom) / 2;
        centred = std::abs(cdx) <= 2 && std::abs(cdy) <= 2;
        sizeOk = (r.right - r.left) == (int)std::lround((flow::W + 2 * flow::M) * 2.0f);
        Key(h, VK_ESCAPE);
    };
    g_settings.uiScale = 2.0f;
    RunDialog(SampleSpec(5), layoutScript);
    g_settings.uiScale = 0;
    Check(centred, L"the window opens in the middle of the screen (off by " + std::to_wstring(cdx) + L", " + std::to_wstring(cdy) + L" px)");
    Check(sizeOk, L"a window size of 200% makes the window exactly twice as wide");

    // 9) 60 apps: the window stays on the screen and the list scrolls
    static bool fits;
    auto manyScript = [](HWND h) {
        RECT w;
        GetWindowRect(h, &w);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        fits = w.top >= mi.rcWork.top && w.bottom <= mi.rcWork.bottom;
        for (int i = 0; i < 40; i++) {
            Key(h, VK_DOWN);   // moving the focus down scrolls the list
        }
        Key(h, VK_RETURN);
    };
    ChecklistResult many = RunDialog(SampleSpec(60), manyScript);
    Check(many.ok && many.checked.size() == 60, L"60 apps work");
    Check(fits, L"the window stays inside the screen with 60 apps");

    // 10) every theme and a missing font open and close without trouble
    static int themesOpened;
    static const wchar_t* const themes[] = {L"auto", L"oled-purple", L"oled-white", L"oled-lime", L"oled-blue", L"midnight", L"graphite-lime",
                                            L"violet-night", L"ember", L"mint", L"paper-light"};
    auto themeScript = [](HWND h) { themesOpened++; Key(h, VK_ESCAPE); };
    for (const wchar_t* t : themes) {
        g_settings.theme = t;
        RunDialog(SampleSpec(3), themeScript);
    }
    g_settings.theme = L"oled-purple";
    g_settings.fontName = L"A Font That Does Not Exist";
    RunDialog(SampleSpec(3), themeScript);
    g_settings.fontName = TestFont();
    Check(themesOpened == 12, L"all 11 themes and a missing font open and close (" + std::to_wstring(themesOpened) + L" of 12)");

    // 10b) icons: a real program gets its own icon, a program that is gone falls back to a drawn tile
    static bool realIcon, missingFallback;
    auto iconScript = [](HWND h) {
        realIcon = flow::g_items.size() == 2 && flow::g_items[0].icon && flow::g_items[0].icon->GetWidth() >= 28;
        missingFallback = flow::g_items.size() == 2 && !flow::g_items[1].icon;
        Key(h, VK_ESCAPE);
    };
    ChecklistSpec withIcons = SampleSpec(0);
    withIcons.items.push_back({L"Notepad", L"", false, L"C:\\Windows\\System32\\notepad.exe"});
    withIcons.items.push_back({L"Uninstalled app", L"", false, L"C:\\Nowhere\\Gone.exe"});
    RunDialog(withIcons, iconScript);
    Check(realIcon, L"a real program shows its own high-resolution icon");
    Check(missingFallback, L"a program that is gone falls back to a drawn tile with its initial");

    // 10c) the window keeps drawing while the worker thread is blocked (power plan, closing apps, reopening apps)
    {
        HANDLE t = CreateThread(nullptr, 0, AutomatorThread, (LPVOID) + [](HWND h) { Key(h, VK_RETURN); }, 0, nullptr);
        ChecklistResult r = flow::Review(SampleSpec(6));
        WaitForSingleObject(t, 5000);
        CloseHandle(t);
        LONG before = flow::g_frames, us0 = flow::g_renderUs;
        Sleep(700);   // the worker does nothing but block, like a slow system call would
        LONG drawn = flow::g_frames - before;
        LONG us = flow::g_renderUs - us0;
        Check(r.ok && drawn >= 25, L"the window keeps drawing while the worker is blocked (" + std::to_wstring(drawn) + L" frames in 0.7 s, " +
                                       std::to_wstring(drawn ? us / drawn / 10 / 100.0 : 0.0).substr(0, 5) + L" ms to draw one)");
        flow::Close();
    }

    // 11) the whole flow: review -> progress -> summary, driven through the same calls the mod uses
    {
        HANDLE t = CreateThread(nullptr, 0, AutomatorThread, (LPVOID) + [](HWND h) { Key(h, VK_RETURN); }, 0, nullptr);
        ChecklistResult r = flow::Review(SampleSpec(3));
        WaitForSingleObject(t, 5000);
        CloseHandle(t);
        bool running = r.ok && flow::Alive() && flow::g_phase == flow::Phase::Running;
        flow::SetState(0, 1);
        flow::SetState(1, 2);
        flow::SetState(2, 1);
        flow::Sleep_(600);
        flow::Finish(L"Game Mode is on", L"Closed 2 of 3 apps.");
        flow::Sleep_(1200);
        bool done = flow::Alive() && flow::g_phase == flow::Phase::Done;
        Check(running, L"after OK the window carries on in its progress view");
        Check(done, L"after the work the window shows the summary");
        flow::Close();
        Check(!flow::Alive(), L"closing the summary closes the window");
    }
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
        Key(h, VK_TAB);     // the first app
        Key(h, VK_SPACE);   // untick it
        Key(h, VK_RETURN);
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
        auto groups = CollectGroups(s);
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
        offItem = flow::g_items.empty() ? L"" : flow::g_items[0].name;
        Key(h, VK_RETURN);
    };
    ToggleWith(offScript);
    WaitFor([] { return Running(L"GmFakeHide"); });
    Check(offItem.find(L"GmFakeHide") != std::wstring::npos, L"the turn-off checklist lists it");
    Check(Running(L"GmFakeHide"), L"and it is reopened");

    // unticked: left alone
    start();
    auto untickThenOk = [](HWND h) {
        Key(h, VK_TAB);     // the first app
        Key(h, VK_SPACE);   // untick it
        Key(h, VK_RETURN);
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

// ---- demo: the whole workflow on fake apps, with a frame of every state ----

// GMT_FONT / GMT_THEME / GMT_SCALE pick the font, theme and window size for the demo.
static std::wstring TestFont() {
    WCHAR b[64] = {};
    GetEnvironmentVariableW(L"GMT_FONT", b, 64);
    return b;
}
static std::wstring TestTheme() {
    WCHAR b[64] = {};
    GetEnvironmentVariableW(L"GMT_THEME", b, 64);
    return b[0] ? b : L"oled-purple";
}
static float TestScale() {
    WCHAR b[16] = {};
    GetEnvironmentVariableW(L"GMT_SCALE", b, 16);
    return (float)_wtof(b);
}

static std::wstring g_demoOut;
static volatile bool g_demoStop = false;

// Copies what is on screen over the inside of the window (without the transparent shadow margin).
static void SnapScreen(HWND h, const std::wstring& path) {
    RECT r;
    GetWindowRect(h, &r);
    int inset = (int)std::lround((flow::M + 1) * flow::g_scale);
    r.left += inset; r.top += inset; r.right -= inset; r.bottom -= inset;
    int w = r.right - r.left, hh = r.bottom - r.top;
    if (w <= 0 || hh <= 0 || !IsWindow(h)) {
        return;
    }
    HDC sd = GetDC(nullptr);
    HDC md = CreateCompatibleDC(sd);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = hh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(sd, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(md, bmp);
    BitBlt(md, 0, 0, w, hh, sd, r.left, r.top, SRCCOPY | CAPTUREBLT);
    SelectObject(md, old);
    BITMAPFILEHEADER fh = {};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + (DWORD)((size_t)w * hh * 4);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD n;
        WriteFile(f, &fh, sizeof(fh), &n, nullptr);
        WriteFile(f, &bi.bmiHeader, sizeof(BITMAPINFOHEADER), &n, nullptr);
        WriteFile(f, bits, (DWORD)((size_t)w * hh * 4), &n, nullptr);
        CloseHandle(f);
    }
    DeleteObject(bmp);
    DeleteDC(md);
    ReleaseDC(nullptr, sd);
}

// Watches the window, takes a frame of every new state, and presses Enter on the checklist like a user.
static DWORD WINAPI DemoSnapper(LPVOID) {
    int n = 0;
    flow::Phase lastPhase = flow::Phase::Closed;
    float lastSnap = -1000;
    while (!g_demoStop) {
        HWND h = FindWindowW(L"GameModeToggleFlow", nullptr);
        if (h && IsWindowVisible(h)) {
            flow::Phase ph = flow::g_phase;
            float now = flow::NowMs();
            if (ph == flow::Phase::Review && lastPhase != ph) {
                Sleep(1100);
                WCHAR num[16];
                wsprintfW(num, L"%02d", ++n);
                SnapScreen(h, g_demoOut + L"\\demo-" + num + L"-review.bmp");
                PostMessageW(h, WM_KEYDOWN, VK_RETURN, 0);
                Sleep(300);
            } else if (ph != flow::Phase::Review && now - lastSnap > 700) {
                WCHAR num[16];
                wsprintfW(num, L"%02d", ++n);
                SnapScreen(h, g_demoOut + L"\\demo-" + num + (ph == flow::Phase::Done ? L"-done.bmp" : L"-running.bmp"));
                lastSnap = now;
            }
            lastPhase = ph;
        }
        Sleep(100);
    }
    return 0;
}

static void DemoWorkflow(const std::wstring& testsDir, const std::wstring& outDir) {
    std::wstring bin = testsDir + L"\\bin";
    std::wstring games = testsDir + L"\\Games";
    g_demoOut = outDir;
    CreateDirectoryW(outDir.c_str(), nullptr);
    WCHAR wd[MAX_PATH];
    GetWindowsDirectoryW(wd, MAX_PATH);
    g_windowsDir = wd;
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_storage = std::wstring(tmp) + L"gmt-test-demo-" + std::to_wstring(GetCurrentProcessId());
    Settings s;
    s.confirm = true;
    s.notify = false;
    s.showProgress = true;
    s.closeOthers = false;  // only the fake apps below
    s.plan = L"none";
    s.timeoutSec = 6;
    s.closeApps = {L"GmFakeWin", L"GmFakeTray", L"GmFakeSlow", L"GmFakeArgs", L"GmFakeDeaf"};
    s.gameFolders = {games};
    s.fontName = TestFont();
    s.theme = TestTheme();
    s.uiScale = TestScale();
    g_settings = s;
    KillFakes();
    Launch(bin + L"\\GmFakeWin.exe", L"");
    Launch(bin + L"\\GmFakeTray.exe", L"--tray");
    Launch(bin + L"\\GmFakeDeaf.exe", L"--deaf");
    Launch(bin + L"\\GmFakeArgs.exe", L"--profile \"My Profile\" -x");
    Launch(bin + L"\\GmFakeSlow.exe", L"--slow 3");
    Launch(games + L"\\GmFakeGame.exe", L"");
    Sleep(2500);
    Out(L"before: " + FakesRunning());
    HANDLE t = CreateThread(nullptr, 0, DemoSnapper, nullptr, 0, nullptr);
    Toggle();  // turn on: checklist, progress, summary
    Out(L"after on: " + FakesRunning());
    flow::Sleep_(4000);  // the summary stays on screen
    Toggle();  // turn off: checklist, progress, summary
    Out(L"after off: " + FakesRunning());
    flow::Sleep_(4000);
    g_demoStop = true;
    WaitForSingleObject(t, 3000);
    CloseHandle(t);
    flow::Close();
    KillFakes();
    RemoveDirectoryW(g_storage.c_str());
}

// Real, generic Windows apps with their own icons, for screenshots.
static ChecklistSpec ShotSpec() {
    ChecklistSpec spec = OnChecklist({}, true, false, L"Ultimate Performance", 1);
    spec.items.push_back({L"Notepad", L"", false, L"C:\\Windows\\System32\\notepad.exe"});
    spec.items.push_back({L"Windows Explorer", L"", false, L"C:\\Windows\\explorer.exe"});
    spec.items.push_back({L"Calculator", L"", false, L"", L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"});
    spec.items.push_back({L"Task Manager", L"background", false, L"C:\\Windows\\System32\\Taskmgr.exe"});
    spec.items.push_back({L"Phone Link", L"", false, L"", L"Microsoft.YourPhone_8wekyb3d8bbwe!App"});
    spec.items.push_back({L"Paint", L"", false, L"C:\\Windows\\System32\\mspaint.exe", L"Microsoft.Paint_8wekyb3d8bbwe!App"});
    return spec;
}

static void ShotSettings() {
    g_settings.confirm = true;
    g_settings.fontName = TestFont();
    g_settings.theme = TestTheme();
    g_settings.uiScale = TestScale();
}

static DWORD WINAPI IconShotThread(LPVOID) {
    HWND h = WaitForDialog();
    if (h) {
        Sleep(1600);
        SnapScreen(h, g_demoOut);
        PostMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
    }
    return 0;
}

// The checklist in one theme (GMT_THEME), for the gallery.
static void IconShot(const std::wstring& out) {
    g_demoOut = out;
    ShotSettings();
    HANDLE t = CreateThread(nullptr, 0, IconShotThread, nullptr, 0, nullptr);
    flow::Review(ShotSpec());
    WaitForSingleObject(t, 6000);
    CloseHandle(t);
    flow::Close();
}

static DWORD WINAPI FlowShotThread(LPVOID) {
    HWND h = WaitForDialog();
    if (h) {
        Sleep(1600);
        SnapScreen(h, g_demoOut + L"\\flow-1-review.bmp");
        PostMessageW(h, WM_KEYDOWN, VK_RETURN, 0);
    }
    return 0;
}

// The three stages of one run: the checklist, apps closing, the summary.
static void FlowShot(const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), nullptr);
    g_demoOut = outDir;
    ShotSettings();
    HANDLE t = CreateThread(nullptr, 0, FlowShotThread, nullptr, 0, nullptr);
    ChecklistResult r = flow::Review(ShotSpec());
    WaitForSingleObject(t, 6000);
    CloseHandle(t);
    if (!r.ok) {
        return;
    }
    Sleep(500);
    flow::SetState(0, 1);
    Sleep(420);
    flow::SetState(1, 1);
    Sleep(420);
    flow::SetState(2, 1);
    Sleep(600);
    HWND h = FindWindowW(L"GameModeToggleFlow", nullptr);
    SnapScreen(h, outDir + L"\\flow-2-running.bmp");
    flow::SetState(3, 1);
    flow::SetState(4, 2);   // one app stays open: the amber warning
    flow::SetState(5, 1);
    Sleep(500);
    flow::Finish(L"Game Mode is on", L"Closed 5 of 6 apps. Still running: Phone Link. Power plan set.");
    Sleep(3200);
    SnapScreen(h, outDir + L"\\flow-3-done.bmp");
    flow::Close();
}

// The window of a running GmFake app, or null.
static HWND FakeWindow(const wchar_t* name) {
    for (const auto& w : ListTopLevelWindows()) {
        if (!IsWindowVisible(w.hwnd) && !IsIconic(w.hwnd)) {
            continue;
        }
        for (const auto& p : ListProcesses()) {
            if (p.pid == w.pid && ExeKey(p.exe) == Lower(name)) {
                return w.hwnd;
            }
        }
    }
    return nullptr;
}

static void BackgroundReopenLive(const std::wstring& testsDir) {
    Out(L"-- L10 reopening in the background");
    std::wstring bin = testsDir + L"\\bin";
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
    g_storage = std::wstring(tmp) + L"gmt-test-bg-" + std::to_wstring(GetCurrentProcessId());
    Snapshot sn;
    sn.startedAt = NowFileTime();
    sn.apps.push_back({L"GmFakeWin", bin + L"\\GmFakeWin.exe", L"", L""});
    for (int pass = 0; pass < 2; pass++) {
        const bool background = pass == 0;
        reopen::Stop();
        KillFakes();
        Settings s;
        s.confirm = false;
        s.notify = false;
        s.showProgress = false;
        s.closeOthers = false;
        s.reopenBackground = background;
        g_settings = s;
        WriteSnapshot(sn);
        Toggle();   // a snapshot exists, so this is the turn-off: the fake app is reopened
        HWND w = nullptr;
        WaitFor([&] { w = FakeWindow(L"GmFakeWin"); return w != nullptr; }, 8000);
        Sleep(1500);   // the watcher gets its moment
        bool iconic = w && IsIconic(w);
        bool hadFocus = GetForegroundWindow() == w;
        if (background) {
            Check(w && iconic && !hadFocus, L"reopened in the background: the window is minimized and does not take the focus");
        } else {
            Check(w && !iconic, L"with the option off the app opens normally");
        }
    }
    // an app that ignores the request to start minimized: started plainly, only the hook can catch its window
    {
        KillFakes();
        reopen::Begin();
        reopen::Expect(bin + L"\\GmFakeWin.exe", L"");
        std::wstring cmd = L"\"" + bin + L"\\GmFakeWin.exe\"";
        STARTUPINFOW si = {sizeof(si)};   // no show-window flags: the app opens normally
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, bin.c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        HWND w = nullptr;
        WaitFor([&] { w = FakeWindow(L"GmFakeWin"); return w != nullptr; }, 8000);
        Sleep(1500);
        Check(w && IsIconic(w), L"an app that ignores the request to start minimized is minimized by the hook as its window appears");
    }
    reopen::Stop();
    KillFakes();
    RemoveDirectoryW(g_storage.c_str());
}

// The real host (windhawk.exe) declares common controls 6 and per-monitor v2 DPI
// awareness in its manifest, and the mod relies on that. Do the same here.
static void EmulateHostManifest() {
    using SetCtx_t = BOOL(WINAPI*)(HANDLE);
    auto setCtx = (SetCtx_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (setCtx) {
        setCtx((HANDLE)-4);
    }
    static const char kManifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
        "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" "
        "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
        "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" "
        "language=\"*\"/></dependentAssembly></dependency></assembly>";
    WCHAR tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring path = std::wstring(tmp) + L"gmt-test-host.manifest";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD wrote = 0;
    WriteFile(f, kManifest, (DWORD)sizeof(kManifest) - 1, &wrote, nullptr);
    CloseHandle(f);
    ACTCTXW ac = {sizeof(ac)};
    ac.lpSource = path.c_str();
    HANDLE h = CreateActCtxW(&ac);
    ULONG_PTR cookie = 0;
    if (h != INVALID_HANDLE_VALUE) {
        ActivateActCtx(h, &cookie);  // stays active for the life of the test
    }
}

int wmain(int argc, wchar_t** argv) {
    EmulateHostManifest();
    std::wstring mode = argc > 1 ? argv[1] : L"unit";
    if (mode == L"flowshot" && argc > 2) {
        FlowShot(argv[2]);
        return 0;
    }
    if (mode == L"uninitparts") {   // where does the shutdown time go?
        WCHAR wd[MAX_PATH];
        GetWindowsDirectoryW(wd, MAX_PATH);
        g_windowsDir = wd;
        ChecklistSpec spec = OnChecklist({}, false, true, L"", 0);
        spec.items.push_back({L"Notepad", L"", false, L"C:\Windows\System32\notepad.exe"});
        g_settings.showProgress = true;
        flow::Direct(spec);
        Sleep(500);
        flow::Finish(L"Game Mode is on", L"Closed 1 app.");
        Sleep(400);
        DWORD tReq = GetTickCount();
        ULONGLONG t0 = GetTickCount64();
        flow::RequestClose();
        while (flow::g_wnd && GetTickCount64() - t0 < 3000) {
            Sleep(1);
        }
        Out(L"window gone after " + std::to_wstring(GetTickCount64() - t0) + L" ms");
        flow::Close();
        ULONGLONG t1 = GetTickCount64();
        flow::Shutdown();
        ULONGLONG t2 = GetTickCount64();
        GdiplusOff();
        ULONGLONG t3 = GetTickCount64();
        Out(L"flow::Close " + std::to_wstring(t1 - t0) + L" ms, rest of flow::Shutdown " + std::to_wstring(t2 - t1) + L" ms, GdiplusOff " + std::to_wstring(t3 - t2) + L" ms");
        return 0;
    }
    if (mode == L"uninit") {   // how long does the tool process take to shut down?
        WCHAR wd[MAX_PATH];
        GetWindowsDirectoryW(wd, MAX_PATH);
        g_windowsDir = wd;
        WCHAR tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        g_storage = std::wstring(tmp) + L"gmt-test-uninit-" + std::to_wstring(GetCurrentProcessId());
        WCHAR only[8] = {};
        const bool single = GetEnvironmentVariableW(L"GMT_ROUND", only, 8) > 0;   // one round per process, like the real tool process
        for (int round = 0; round < 3; round++) {
            if (single && _wtoi(only) != round) {
                continue;
            }
            WhTool_ModInit();
            Sleep(600);
            if (round >= 1) {   // a summary window is still open, with the window thread running
                ChecklistSpec spec = OnChecklist({}, false, true, L"", 0);
                spec.items.push_back({L"Notepad", L"", false, L"C:\Windows\System32\notepad.exe"});
                flow::Direct(spec);
                Sleep(500);
                flow::Finish(L"Game Mode is on", L"Closed 1 app.");
                Sleep(round == 2 ? 2500 : 300);
            }
            ULONGLONG t0 = GetTickCount64();
            WhTool_ModUninit();
            Out(L"round " + std::to_wstring(round) + (round ? L" (window open)" : L" (idle)") + L": shut down in " + std::to_wstring(GetTickCount64() - t0) + L" ms");
        }
        return 0;
    }
    if (mode == L"liveb" && argc > 2) {   // only the background-reopen test
        BackgroundReopenLive(argv[2]);
        Out(L"passed " + std::to_wstring(g_pass) + L", failed " + std::to_wstring(g_fail));
        return g_fail ? 1 : 0;
    }
    if (mode == L"livew" && argc > 2) {   // only the window-less app tests
        WindowlessLive(argv[2]);
        Out(L"passed " + std::to_wstring(g_pass) + L", failed " + std::to_wstring(g_fail));
        return g_fail ? 1 : 0;
    }
    if (mode == L"iconshot" && argc > 2) {
        IconShot(argv[2]);
        return 0;
    }
    if (mode == L"demo" && argc > 3) {
        DemoWorkflow(argv[2], argv[3]);
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
        BackgroundReopenLive(argv[2]);
    } else {
        Out(L"usage: test.exe unit | probe | live <tests dir>");
        return 2;
    }
    Out(L"");
    Out(L"passed " + std::to_wstring(g_pass) + L", failed " + std::to_wstring(g_fail));
    return g_fail ? 1 : 0;
}
