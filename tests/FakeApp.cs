using System; using System.IO; using System.Windows.Forms; using System.Threading;
public class FakeApp : Form {
    static string Log;
    // Several fakes close at the same moment and share one log file; a collision must never crash a fake.
    static void LogLine(string text) { for (int i = 0; i < 5; i++) { try { File.AppendAllText(Log, text); return; } catch (IOException) { Thread.Sleep(20); } } }
    static string Me;
    static bool HideOnClose;   // --hide: like WhatsApp or Phone Link, a close request only hides the window
    static bool Quitting;      // set by the quit file: a real exit is allowed
    int slow;
    public FakeApp(bool tray, int slowSec) {
        slow = slowSec; Text = Me + " window"; Width = 260; Height = 90;
        if (tray) { ShowInTaskbar = false; }
        FormClosing += (s, e) => { if (HideOnClose && !Quitting) { e.Cancel = true; Hide(); return; } if (slow > 0) Thread.Sleep(slow * 1000); LogLine(DateTime.Now.ToString("HH:mm:ss") + " " + Me + " clean-exit (draft saved)" + Environment.NewLine); };
    }
    [STAThread] public static void Main(string[] args) {
        Me = Path.GetFileNameWithoutExtension(Application.ExecutablePath);
        Log = Path.Combine(Path.GetDirectoryName(Application.ExecutablePath), "..\\out\\events.log");
        LogLine(DateTime.Now.ToString("HH:mm:ss") + " " + Me + " start args=[" + string.Join(" ", args) + "]" + Environment.NewLine);
        HideOnClose = Array.IndexOf(args, "--hide") >= 0;
        bool tray = Array.IndexOf(args, "--tray") >= 0;
        int slowSec = 0; int i = Array.IndexOf(args, "--slow"); if (i >= 0 && i + 1 < args.Length) slowSec = int.Parse(args[i + 1]);
        if (Array.IndexOf(args, "--quit") >= 0) {   // like Rainmeter !Quit / Everything -exit: ask the running instance to exit
            foreach (var p in System.Diagnostics.Process.GetProcessesByName(Me)) if (p.Id != System.Diagnostics.Process.GetCurrentProcess().Id) { File.WriteAllText(Path.Combine(Path.GetTempPath(), Me + ".quit"), "1"); }
            return;
        }
        var f = new FakeApp(tray, slowSec);
        var t = new System.Windows.Forms.Timer { Interval = 300 };
        string q = Path.Combine(Path.GetTempPath(), Me + ".quit");
        bool deafMode = Array.IndexOf(args, "--deaf") >= 0;
        t.Tick += (s, e) => { if (File.Exists(q)) { File.Delete(q); Quitting = true; if (deafMode) { LogLine(DateTime.Now.ToString("HH:mm:ss") + " " + Me + " clean-exit (draft saved)" + Environment.NewLine); Application.ExitThread(); } else f.Close(); } };
        t.Start();
        bool deaf = Array.IndexOf(args, "--deaf") >= 0;
        if (deaf) { Application.Run(); }   // no window of its own: ignores WM_CLOSE, needs a quit command or a kill
        else if (tray) { f.Shown += (s, e) => f.Hide(); Application.Run(f); }   // hidden main window, like a tray app
        else Application.Run(f);
    }
}



