// Interactive working model of the Game Mode flow. Fake apps only: nothing on your PC is closed.
//   Mouse: click a card to toggle it, Turn on, Cancel, Done.   Keys: Tab/Shift+Tab move, Space toggles, Enter confirms,
//   Esc cancels/closes, R replays the flow.
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
using namespace Gdiplus;

static std::wstring gFont = L"Geist Mono";

static float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static float EaseOut(float t) { t = Clamp01(t); return 1 - std::pow(1 - t, 3.0f); }
static float EaseInOut(float t) { t = Clamp01(t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }
static float Overshoot(float t) { t = Clamp01(t); const float c1 = 1.70158f, c3 = c1 + 1; return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f); }

static const Color kBg(255, 0, 0, 0), kCard(255, 12, 11, 16), kCardLine(255, 32, 27, 42), kCardHot(255, 22, 19, 30),
    kText(255, 245, 243, 255), kMuted(255, 142, 136, 160), kAccent(255, 167, 139, 250), kAccentHot(255, 190, 170, 255),
    kAccentDeep(255, 124, 58, 237), kOnAccent(255, 18, 7, 43), kAmber(255, 251, 191, 36), kTrack(255, 28, 24, 38);

static Color A(Color c, float a) { return Color((BYTE)std::clamp((int)(a * c.GetA()), 0, 255), c.GetR(), c.GetG(), c.GetB()); }

static void RR(GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    float d = r * 2;
    if (d <= 0.1f) { p.AddRectangle(RectF(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
static void FillRR(Graphics& g, Color c, float x, float y, float w, float h, float r) { GraphicsPath p; RR(p, x, y, w, h, r); SolidBrush b(c); g.FillPath(&b, &p); }
static void StrokeRR(Graphics& g, Color c, float width, float x, float y, float w, float h, float r) { GraphicsPath p; RR(p, x, y, w, h, r); Pen pen(c, width); g.DrawPath(&pen, &p); }
static void Str(Graphics& g, const std::wstring& s, Font& f, float x, float y, Color c, float alpha, StringAlignment al = StringAlignmentNear, float boxW = 0) {
    if (alpha <= 0.01f) return;
    SolidBrush b(A(c, alpha));
    static StringFormat* sfs[3] = {nullptr, nullptr, nullptr};   // built once
    int k = al == StringAlignmentNear ? 0 : al == StringAlignmentCenter ? 1 : 2;
    if (!sfs[k]) { sfs[k] = new StringFormat(); sfs[k]->SetAlignment(al); sfs[k]->SetFormatFlags(StringFormatFlagsNoWrap); }
    if (al == StringAlignmentNear) g.DrawString(s.c_str(), -1, &f, PointF(x, y), sfs[k], &b);
    else g.DrawString(s.c_str(), -1, &f, RectF(x, y, boxW, 40), sfs[k], &b);
}
static float TextW(Graphics& g, const std::wstring& s, Font& f) {
    static std::map<std::pair<std::wstring, Font*>, float> cache;   // measuring text every frame is slow
    auto key = std::make_pair(s, &f);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    RectF r; g.MeasureString(s.c_str(), -1, &f, PointF(0, 0), &r);
    cache[key] = r.Width;
    return r.Width;
}
static float gScale = 1.0f;
// A glow is baked once into a bitmap and then drawn with a changing opacity: much cheaper per frame.
static Bitmap* gGlowBmp[3] = {nullptr, nullptr, nullptr};
static std::wstring FitStr(Graphics& g, const std::wstring& s, Font& f, float maxW) {
    if (TextW(g, s, f) <= maxW) return s;
    std::wstring t = s;
    while (t.size() > 1 && TextW(g, t + L"\u2026", f) > maxW) t.pop_back();
    return t + L"\u2026";
}
static void Glow(Graphics& g, int slot, float cx, float cy, float rx, float ry, Color c, float alpha) {
    if (alpha <= 0.01f) return;
    if (!gGlowBmp[slot]) {
        int w = (int)(rx * 2 * gScale) + 2, h = (int)(ry * 2 * gScale) + 2;
        gGlowBmp[slot] = new Bitmap(w, h, PixelFormat32bppPARGB);
        Graphics gg(gGlowBmp[slot]);
        gg.SetSmoothingMode(SmoothingModeAntiAlias);
        gg.Clear(Color(0, 0, 0, 0));
        GraphicsPath p; p.AddEllipse(0.0f, 0.0f, (float)w, (float)h);
        PathGradientBrush pg(&p); pg.SetCenterColor(Color(255, c.GetR(), c.GetG(), c.GetB()));
        Color edge(0, c.GetR(), c.GetG(), c.GetB()); int n = 1; pg.SetSurroundColors(&edge, &n);
        gg.FillPath(&pg, &p);
    }
    ColorMatrix cm = {1, 0, 0, 0, 0,  0, 1, 0, 0, 0,  0, 0, 1, 0, 0,  0, 0, 0, std::min(1.0f, alpha), 0,  0, 0, 0, 0, 1};
    ImageAttributes ia; ia.SetColorMatrix(&cm);
    g.DrawImage(gGlowBmp[slot], RectF(cx - rx, cy - ry, rx * 2, ry * 2), 0.0f, 0.0f, (float)gGlowBmp[slot]->GetWidth(), (float)gGlowBmp[slot]->GetHeight(), UnitPixel, &ia);
}
static void CheckMark(Graphics& g, float cx, float cy, float s, float u, Color c, float width) {
    PointF p0(cx - 0.34f * s, cy + 0.02f * s), p1(cx - 0.09f * s, cy + 0.27f * s), p2(cx + 0.38f * s, cy - 0.26f * s);
    float l1 = std::hypot(p1.X - p0.X, p1.Y - p0.Y), l2 = std::hypot(p2.X - p1.X, p2.Y - p1.Y);
    float total = (l1 + l2) * Clamp01(u);
    Pen pen(c, width); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    if (total <= l1) { float k = total / l1; g.DrawLine(&pen, p0, PointF(Lerp(p0.X, p1.X, k), Lerp(p0.Y, p1.Y, k))); }
    else { float k = (total - l1) / l2; g.DrawLine(&pen, p0, p1); g.DrawLine(&pen, p1, PointF(Lerp(p1.X, p2.X, k), Lerp(p1.Y, p2.Y, k))); }
}

// ---------------- model ----------------
struct App { const wchar_t* name; Color icon; const wchar_t* tag; bool ticked; bool leftRunning; float closeAt; };
static std::vector<App> gApps;
static void ResetApps() {
    gApps = {
        {L"Discord", Color(255, 88, 101, 242), L"", true, false, 0},
        {L"Spotify", Color(255, 29, 185, 84), L"", true, false, 0},
        {L"WhatsApp", Color(255, 37, 211, 102), L"background", true, false, 0},
        {L"Dropbox", Color(255, 0, 97, 255), L"background", true, true, 0},
        {L"Telegram", Color(255, 42, 171, 238), L"", false, false, 0},
    };
}

static const float W = 520;
static const float M = 26;    // transparent margin around the window for its shadow
static const float TB = 44;   // the drawn title strip: part of the same surface as the content
static const float T_P1 = 650, T_P2 = 950;
static float gNow = 0;            // ms since the flow started
static float gClickAt = -1;       // when Turn on was pressed
static float gDoneAt = -1;        // when the last app finished
static int gHover = -1, gPressed = -1, gFocus = 6;  // ids: 0..4 cards, 5 cancel, 6 turn on, 7 done
struct Hit { RectF r; int id; };
static std::vector<Hit> gHits;
static float gBody = 480;
static float gListCap = 300;      // the most the list may show at once (logical px); longer lists scroll
static float gScroll = 0, gScrollTarget = 0;

struct Lay { float p1, p2, listH, viewH, body, footerH; std::vector<float> fac; };
static const float rowH = 52, gap = 8, y0 = 104;

static Lay Layout(float t) {
    Lay L;
    L.p1 = gClickAt >= 0 ? EaseInOut((t - gClickAt) / T_P1) : 0;
    L.p2 = gDoneAt >= 0 ? EaseInOut((t - gDoneAt) / T_P2) : 0;
    L.listH = 0;
    L.fac.resize(gApps.size());
    for (size_t i = 0; i < gApps.size(); i++) {
        float f = 1;
        if (!gApps[i].ticked) f *= 1 - L.p1;
        if (gDoneAt >= 0) f *= 1 - EaseInOut((t - gDoneAt - 45.0f * i) / 520);
        L.fac[i] = f;
        L.listH += (rowH + gap) * f;
    }
    L.viewH = std::min(L.listH, gListCap);
    L.footerH = Lerp(86, 92, L.p1);
    float bodyRun = y0 + L.viewH + L.footerH;
    L.body = Lerp(bodyRun, 262, L.p2);
    return L;
}

static void AddHit(float x, float y, float w, float h, int id) { gHits.push_back({RectF(x, y, w, h), id}); }

// The window background (black plus the purple glows) is baked into one bitmap and re-baked only when the glow's
// opacity actually changes (during the "done" moment), so almost every frame just blits it.
static const float kBgH = 840;   // logical height baked, starting TB above the content (covers the tallest window)
static Bitmap* gBg = nullptr;
static int gBgKey = -1;
static void DrawBackground(Graphics& g, float p2) {
    int key = (int)std::lround(p2 * 48);
    int wPx = (int)std::ceil(W * gScale), hPx = (int)std::ceil((kBgH + TB) * gScale);
    if (!gBg || key != gBgKey) {
        delete gBg;
        gBg = new Bitmap(wPx, hPx, PixelFormat32bppPARGB);
        Graphics gb(gBg);
        gb.SetSmoothingMode(SmoothingModeAntiAlias);
        gb.ScaleTransform(gScale, gScale);
        SolidBrush bg(kBg); gb.FillRectangle(&bg, 0.0f, 0.0f, W + 2, kBgH + TB + 2);
        float q = key / 48.0f;
        Glow(gb, 0, W * 0.3f, TB + 60, 300, 130, kAccentDeep, Lerp(0.20f, 0.38f, q));
        Glow(gb, 1, W * 0.5f, TB + 130, 260, 150, kAccent, 0.10f * q);
        gBgKey = key;
    }
    g.DrawImage(gBg, RectF(0, -TB, wPx / gScale, hPx / gScale), 0.0f, 0.0f, (float)wPx, (float)hPx, UnitPixel);
}

struct Fonts {
    FontFamily* ff;
    Font *title, *body, *row, *tag, *btn, *big, *count, *small;
};
static Fonts* GetFonts() {
    static Fonts* f = nullptr;
    if (f) return f;
    FontFamily* ff = new FontFamily(gFont.c_str());
    if (!ff->IsAvailable()) { delete ff; ff = new FontFamily(L"Segoe UI Variable Text"); }
    if (!ff->IsAvailable()) { delete ff; ff = new FontFamily(L"Segoe UI"); }
    f = new Fonts{ff, new Font(ff, 23, FontStyleBold, UnitPixel), new Font(ff, 14, FontStyleRegular, UnitPixel),
                  new Font(ff, 15, FontStyleRegular, UnitPixel), new Font(ff, 11, FontStyleRegular, UnitPixel),
                  new Font(ff, 14, FontStyleBold, UnitPixel), new Font(ff, 26, FontStyleBold, UnitPixel),
                  new Font(ff, 13, FontStyleRegular, UnitPixel), new Font(ff, 13, FontStyleRegular, UnitPixel)};
    return f;
}

static void Frame(Graphics& g, float t) {
    Lay L = Layout(t);
    float p1 = L.p1, p2 = L.p2;
    gBody = L.body;
    gHits.clear();
    Fonts* F = GetFonts();
    Font& fTitle = *F->title; Font& fBody = *F->body; Font& fRow = *F->row; Font& fTag = *F->tag;
    Font& fBtn = *F->btn; Font& fBig = *F->big; Font& fCount = *F->count;

    int nTicked = 0, nClose = 0;
    for (auto& a : gApps) { nTicked += a.ticked; nClose += a.ticked && !a.leftRunning; }
    float closed = 0;
    for (auto& a : gApps) if (a.ticked && !a.leftRunning && gClickAt >= 0 && t >= a.closeAt) closed += 1;
    float prog = 0;
    if (gClickAt >= 0 && nTicked > 0) {
        float c = 0;
        for (auto& a : gApps) if (a.ticked && !a.leftRunning) c += EaseOut((t - (a.closeAt - 350)) / 350);
        prog = c / nTicked;
        if (gDoneAt >= 0) prog = Lerp(prog, 1.0f, EaseOut((t - gDoneAt) / 500));
    }

    DrawBackground(g, p2);

    float cx0 = 28, cw = W - 56, top = 0;

    // header
    float tA = 1 - EaseOut(p1 * 2.2f);
    float tB = EaseOut((p1 - 0.35f) / 0.65f) * (1 - Clamp01(p2 * 2.2f));
    float enterHdr = EaseOut(t / 420);
    wchar_t buf[120];
    Str(g, L"Turn on Game Mode?", fTitle, cx0, top + 26 + (1 - enterHdr) * 8 + p1 * -6, kText, tA * enterHdr);
    if (nTicked == 0) swprintf(buf, 120, L"Nothing is ticked, so nothing will close.");
    else swprintf(buf, 120, L"%d app%ls will close. The hotkey brings them back.", nTicked, nTicked == 1 ? L"" : L"s");
    Str(g, FitStr(g, buf, fBody, W - 56), fBody, cx0, top + 62 + (1 - enterHdr) * 8, kMuted, tA * enterHdr);
    Str(g, L"Closing apps", fTitle, cx0, top + 26 + (1 - tB) * 8, kText, tB);
    swprintf(buf, 120, L"%d of %d closed", (int)closed, nClose);
    Str(g, FitStr(g, buf, fBody, W - 56), fBody, cx0, top + 62 + (1 - tB) * 8, kMuted, tB);

    // scrolling: smooth, clamped, follows the focus in review and the first unfinished app while closing
    static float prevT = 0;
    float dt = t - prevT; if (dt < 0 || dt > 100) dt = 16; prevT = t;
    float maxScroll = std::max(0.0f, L.listH - L.viewH);
    {
        static int lastFocus = -2;
        if (gFocus != lastFocus) {
            lastFocus = gFocus;
            if (gClickAt < 0 && gFocus >= 0 && gFocus < (int)gApps.size()) {
                float yRow = gFocus * (rowH + gap);
                if (yRow < gScrollTarget) gScrollTarget = yRow;
                else if (yRow + rowH > gScrollTarget + L.viewH) gScrollTarget = yRow + rowH - L.viewH;
            }
        }
        if (gClickAt >= 0 && gDoneAt < 0 && maxScroll > 0) {
            float pos = 0;
            for (size_t i = 0; i < gApps.size(); i++) {
                if (gApps[i].ticked && !(!gApps[i].leftRunning && t >= gApps[i].closeAt) && !(gApps[i].leftRunning && t >= gApps[i].closeAt)) { gScrollTarget = pos - L.viewH * 0.35f; break; }
                pos += (rowH + gap) * L.fac[i];
            }
        }
        gScrollTarget = std::clamp(gScrollTarget, 0.0f, maxScroll);
        gScroll += (gScrollTarget - gScroll) * (1 - std::exp(-dt / 70.0f));
        gScroll = std::clamp(gScroll, 0.0f, maxScroll);
    }
    GraphicsState listState = g.Save();
    float vpTop = top + y0, vpH = L.viewH;
    g.SetClip(RectF(cx0 - 4, vpTop, cw + 8, vpH), CombineModeIntersect);
    float ry = vpTop - gScroll;
    for (size_t i = 0; i < gApps.size(); i++) {
        const App& a = gApps[i];
        float f = L.fac[i];
        if (f <= 0.002f) continue;
        float enter = EaseOut((t - 120 - 70.0f * i) / 420);
        float alpha = enter * std::min(1.0f, f * 1.6f);
        float slide = (1 - enter) * 14;
        float yy = ry + slide;
        float hh = rowH * (f < 1 ? std::max(0.35f, f) : 1.0f);
        bool interactive = gClickAt < 0 && enter > 0.9f;
        bool hot = interactive && (gHover == (int)i);
        FillRR(g, A(hot ? kCardHot : kCard, alpha), cx0, yy, cw, hh, 14);
        StrokeRR(g, A(hot ? Color(255, 70, 58, 96) : kCardLine, alpha), 1, cx0 + 0.5f, yy + 0.5f, cw - 1, hh - 1, 14);
        if (gFocus == (int)i && gClickAt < 0) StrokeRR(g, A(kAccent, alpha * 0.9f), 1.6f, cx0 - 1.5f, yy - 1.5f, cw + 3, hh + 3, 16);
        if (interactive) {   // only the part inside the viewport can be clicked
            float hy0 = std::max(yy, vpTop), hy1 = std::min(yy + rowH, vpTop + vpH);
            if (hy1 > hy0) AddHit(cx0, hy0, cw, hy1 - hy0, (int)i);
        }
        GraphicsState rs = g.Save();
        GraphicsPath clipP; RR(clipP, cx0, yy, cw, hh, 14);
        g.SetClip(&clipP, CombineModeIntersect);
        float content = alpha * (f < 0.55f ? f / 0.55f : 1.0f);
        FillRR(g, A(a.icon, content * (a.ticked || gClickAt >= 0 ? 1.0f : 0.55f)), cx0 + 14, yy + 13, 26, 26, 7);
        float tagW = a.tag[0] ? TextW(g, a.tag, fTag) + 14 + 10 : 0;
        std::wstring nm = FitStr(g, a.name, fRow, cw - 52 - 70 - tagW);
        Str(g, nm, fRow, cx0 + 52, yy + 14, kText, content * (a.ticked || gClickAt >= 0 ? 1.0f : 0.6f));
        if (a.tag[0]) {
            float nw = TextW(g, nm, fRow);
            float tx = cx0 + 52 + nw + 10, tw = TextW(g, a.tag, fTag) + 14;
            StrokeRR(g, A(kCardLine, content), 1, tx, yy + 16, tw, 20, 10);
            Str(g, a.tag, fTag, tx + 7, yy + 19, kMuted, content);
        }
        float cxr = cx0 + cw - 14 - 21, cyr = yy + rowH / 2;
        float toggleA = (1 - EaseOut(p1 * 2.5f)) * content;
        if (toggleA > 0.01f) {
            // animated knob
            float on = a.ticked ? 1.0f : 0.0f;
            FillRR(g, A(a.ticked ? kAccent : kTrack, toggleA), cxr - 21, cyr - 12, 42, 24, 12);
            float kx = on ? cxr + 9 : cxr - 9;
            SolidBrush kb(A(on ? kOnAccent : Color(255, 120, 114, 138), toggleA));
            g.FillEllipse(&kb, kx - 8.5f, cyr - 8.5f, 17.0f, 17.0f);
        }
        if (a.ticked && gClickAt >= 0) {
            float runA = EaseOut((p1 - 0.3f) / 0.7f) * content;
            bool closedNow = !a.leftRunning && t >= a.closeAt;
            bool gaveUp = a.leftRunning && t >= a.closeAt;
            float doneU = closedNow ? EaseOut((t - a.closeAt) / 380) : 0;
            float gaveU = gaveUp ? EaseOut((t - a.closeAt) / 300) : 0;
            float spinA = runA * (1 - std::max(doneU, gaveU));
            if (spinA > 0.01f) {
                Pen track(A(kTrack, spinA), 2.5f);
                g.DrawEllipse(&track, cxr - 9, cyr - 9, 18.0f, 18.0f);
                Pen sp(A(kAccent, spinA), 2.5f); sp.SetStartCap(LineCapRound); sp.SetEndCap(LineCapRound);
                float ang = std::fmod(t * 0.42f + i * 70, 360.0f);
                g.DrawArc(&sp, cxr - 9, cyr - 9, 18.0f, 18.0f, ang, 250.0f);
            }
            if (closedNow) {
                float s = Overshoot(doneU);
                SolidBrush cb(A(kAccent, runA * doneU));
                float rad = 11 * s; g.FillEllipse(&cb, cxr - rad, cyr - rad, rad * 2, rad * 2);
                CheckMark(g, cxr, cyr, 16, EaseOut((t - a.closeAt - 60) / 320), A(kOnAccent, runA), 2.2f);
            }
            if (gaveUp) {
                SolidBrush cb(A(Color(255, 60, 44, 8), runA * gaveU));
                g.FillEllipse(&cb, cxr - 11, cyr - 11, 22.0f, 22.0f);
                Pen ap(A(kAmber, runA * gaveU), 2.0f); ap.SetStartCap(LineCapRound); ap.SetEndCap(LineCapRound);
                g.DrawEllipse(&ap, cxr - 10.5f, cyr - 10.5f, 21.0f, 21.0f);
                g.DrawLine(&ap, cxr, cyr - 5, cxr, cyr + 1);
                SolidBrush ad(A(kAmber, runA * gaveU)); g.FillEllipse(&ad, cxr - 1.3f, cyr + 4.2f, 2.6f, 2.6f);
            }
        }
        g.Restore(rs);
        ry += (rowH + gap) * f;
    }
    g.Restore(listState);
    if (maxScroll > 1) {   // soft fade at the cut edges and a thin scroll indicator
        float fa = 22;
        if (gScroll > 1) { LinearGradientBrush lgT(PointF(0, vpTop), PointF(0, vpTop + fa), Color(255, 0, 0, 0), Color(0, 0, 0, 0)); g.FillRectangle(&lgT, cx0 - 4, vpTop, cw + 8, fa); }
        if (gScroll < maxScroll - 1) { LinearGradientBrush lgB(PointF(0, vpTop + vpH - fa), PointF(0, vpTop + vpH), Color(0, 0, 0, 0), Color(255, 0, 0, 0)); g.FillRectangle(&lgB, cx0 - 4, vpTop + vpH - fa, cw + 8, fa); }
        float thumbH = std::max(28.0f, vpH * vpH / L.listH);
        float thumbY = vpTop + (vpH - thumbH) * (gScroll / maxScroll);
        FillRR(g, A(kMuted, 0.45f), W - 12, thumbY, 3, thumbH, 1.5f);
    }

    // footer: review buttons
    float footTop = top + y0 + L.viewH;
    float btnA = (1 - EaseOut(p1 * 2.4f)) * EaseOut((t - 450) / 400);
    float btnY = footTop + 22;
    if (btnA > 0.01f) {
        bool onHot = gHover == 6, onDown = gPressed == 6 && onHot, cancelHot = gHover == 5, cancelDown = gPressed == 5 && cancelHot;
        bool disabled = nTicked == 0;
        Color fill = disabled ? kTrack : onDown ? kAccentDeep : onHot ? kAccentHot : kAccent;
        FillRR(g, A(fill, btnA), cx0 + cw - 132, btnY, 132, 42, 12);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush ob(A(disabled ? kMuted : kOnAccent, btnA));
        g.DrawString(L"Turn on", -1, &fBtn, RectF(cx0 + cw - 132, btnY, 132, 42), &sf, &ob);
        float cxB = cx0 + cw - 132 - 12 - 108;
        FillRR(g, A(cancelDown ? Color(255, 30, 26, 40) : cancelHot ? kCardHot : kCard, btnA), cxB, btnY, 108, 42, 12);
        StrokeRR(g, A(cancelHot ? Color(255, 70, 58, 96) : kCardLine, btnA), 1, cxB + 0.5f, btnY + 0.5f, 107, 41, 12);
        SolidBrush tb2(A(kText, btnA));
        g.DrawString(L"Cancel", -1, &fBtn, RectF(cxB, btnY, 108, 42), &sf, &tb2);
        if (gFocus == 6) StrokeRR(g, A(kAccentHot, btnA), 1.6f, cx0 + cw - 132 - 2.5f, btnY - 2.5f, 137, 47, 14);
        if (gFocus == 5) StrokeRR(g, A(kAccent, btnA), 1.6f, cxB - 2.5f, btnY - 2.5f, 113, 47, 14);
        Str(g, L"Enter to confirm   Esc to cancel", fTag, cx0, btnY + 14, kMuted, btnA * 0.8f);
        if (btnA > 0.6f && gClickAt < 0) { AddHit(cx0 + cw - 132, btnY, 132, 42, 6); AddHit(cxB, btnY, 108, 42, 5); }
    }

    // progress bar -> Done button
    float barA = EaseOut((p1 - 0.35f) / 0.65f);
    float barY = footTop + 30, barX = cx0, barW = cw, barH = 8;
    float btnW = 150, btnH = 44;
    float doneBtnX = (W - btnW) / 2, doneBtnY = top + 262 - btnH - 28;
    float m = EaseInOut((p2 - 0.45f) / 0.55f);
    float bx = Lerp(barX, doneBtnX, m), bw = Lerp(barW, btnW, m), by = Lerp(barY, doneBtnY, m), bh = Lerp(barH, btnH, m), br = Lerp(4, 13, m);
    if (barA > 0.01f) {
        float cA = barA * (1 - Clamp01(p2 * 3));
        swprintf(buf, 120, L"%d%%", (int)(prog * 100));
        Str(g, L"Asking apps to close politely", fCount, cx0, barY - 22, kMuted, cA);
        Str(g, buf, fCount, cx0, barY - 22, kText, cA, StringAlignmentFar, cw);
        FillRR(g, A(kTrack, barA * (1 - m)), barX, barY, barW, barH, 4);
        float fillW = std::max(barH, barW * prog);
        float glowA = 0.35f * (1 - m);
        if (glowA > 0.01f) FillRR(g, A(kAccent, barA * glowA * 0.5f), barX - 2, barY - 3, fillW + 4, barH + 6, 7);
        bool dHot = m > 0.99f && gHover == 7, dDown = dHot && gPressed == 7;
        Color fc = m > 0.5f ? (dDown ? kAccentDeep : dHot ? kAccentHot : kAccent) : kAccent;
        if (m < 0.001f) FillRR(g, A(kAccent, barA), barX, barY, fillW, barH, 4);
        else FillRR(g, A(fc, barA), bx, by, bw, bh, br);
        if (m > 0.55f) {
            float ta = Clamp01((m - 0.55f) / 0.45f);
            StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
            SolidBrush ob(A(kOnAccent, ta));
            g.DrawString(L"Done", -1, &fBtn, RectF(bx, by, bw, bh), &sf, &ob);
        }
        if (m > 0.99f) { AddHit(bx, by, bw, bh, 7); if (gFocus == 7) StrokeRR(g, A(kAccentHot, 1.0f), 1.6f, bx - 2.5f, by - 2.5f, bw + 5, bh + 5, 15); }
    }

    // done: badge, rings, headline
    if (p2 > 0.2f) {
        float u = (p2 - 0.2f) / 0.8f;
        float bcx = W / 2, bcy = top + 62;
        for (int k = 0; k < 2; k++) {
            float ru = Clamp01((u - 0.15f * k - 0.1f) / 0.9f);
            if (ru > 0 && ru < 1) {
                Pen rp(A(kAccent, (1 - ru) * 0.45f), 2.0f);
                float rr = 34 + ru * 46; g.DrawEllipse(&rp, bcx - rr, bcy - rr, rr * 2, rr * 2);
            }
        }
        float s = Overshoot((u - 0.05f) / 0.55f);
        Glow(g, 2, bcx, bcy, 70, 70, kAccent, 0.55f * EaseOut(u) * std::min(1.0f, s));
        GraphicsPath bp; float rad = 32 * s; bp.AddEllipse(bcx - rad, bcy - rad, rad * 2, rad * 2);
        LinearGradientBrush lg(PointF(bcx - rad, bcy - rad), PointF(bcx + rad, bcy + rad), Color(255, 196, 181, 253), Color(255, 124, 58, 237));
        g.FillPath(&lg, &bp);
        CheckMark(g, bcx, bcy, 44, EaseOut((u - 0.35f) / 0.5f), Color(255, 255, 255, 255), 4.2f);
        float ha = EaseOut((u - 0.4f) / 0.6f);
        Str(g, L"Game Mode is on", fBig, 0, top + 112 + (1 - ha) * 8, kText, ha, StringAlignmentCenter, W);
        bool left = false; for (auto& a : gApps) left = left || (a.ticked && a.leftRunning);
        if (left) swprintf(buf, 120, L"Closed %d app%ls. Dropbox is still running.", nClose, nClose == 1 ? L"" : L"s");
        else swprintf(buf, 120, L"Closed %d app%ls.", nClose, nClose == 1 ? L"" : L"s");
        Str(g, FitStr(g, buf, fBody, W - 56), fBody, 0, top + 152 + (1 - ha) * 8, kMuted, ha, StringAlignmentCenter, W);
    }
}

// ---------------- unibody title strip ----------------
static int gTitleHover = -1;  // 8 = minimise, 9 = close
static int TitleHit(float x, float y) {
    if (y < 0 || y >= TB) return -1;
    if (x >= W - 46) return 9;
    if (x >= W - 92) return 8;
    return -1;
}
static void DrawTitle(Graphics& g, bool active) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    Font& f = *GetFonts()->small;
    // icon
    GraphicsPath sq; RR(sq, 18, 14, 17, 17, 4.5f);
    LinearGradientBrush lg(PointF(18, 14), PointF(35, 31), Color(255, 196, 181, 253), Color(255, 124, 58, 237));
    g.FillPath(&lg, &sq);
    PointF bolt[] = {{28.2f, 16.2f}, {22.8f, 23.6f}, {26.4f, 23.6f}, {24.6f, 29.2f}, {30.6f, 21.4f}, {27.0f, 21.4f}};
    SolidBrush w(Color(255, 255, 255, 255)); g.FillPolygon(&w, bolt, 6);
    Str(g, L"Game Mode", f, 46, 12, kMuted, active ? 1.0f : 0.6f);
    // buttons: quiet until hovered
    float cx = W - 23, cy = TB / 2;
    if (gTitleHover == 9) { FillRR(g, Color(255, 229, 72, 77), W - 42, 8, 34, 28, 8); }
    else if (gTitleHover == 8) { FillRR(g, kCardHot, W - 88, 8, 34, 28, 8); }
    Pen x(gTitleHover == 9 ? Color(255, 255, 255, 255) : A(kMuted, 1), 1.4f);
    x.SetStartCap(LineCapRound); x.SetEndCap(LineCapRound);
    g.DrawLine(&x, cx - 5, cy - 5, cx + 5, cy + 5);
    g.DrawLine(&x, cx + 5, cy - 5, cx - 5, cy + 5);
    Pen mn(A(kMuted, 1), 1.4f); mn.SetStartCap(LineCapRound); mn.SetEndCap(LineCapRound);
    g.DrawLine(&mn, W - 71, cy + 0.5f, W - 61, cy + 0.5f);
}

// ---------------- window ----------------
static ULONG_PTR gTok;
static HICON gIconS, gIconB;
static LARGE_INTEGER gFreq, gStartQpc;
static HWND gWnd;
static HDC gMemDC;
static HBITMAP gDib;
static Bitmap* gSurface;
static int gMaxW, gMaxH;
static double gCx, gCy;        // window centre on screen (physical px)
static int gLastL = -99999, gLastT = -99999;
static double gLastRender = 0;
static void ApplyScale(HWND h, float scale);

static float NowMs() {
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return (float)((q.QuadPart - gStartQpc.QuadPart) * 1000.0 / gFreq.QuadPart);
}

static HICON MakeIcon(int size) {
    HICON icon = nullptr;
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp); g.SetSmoothingMode(SmoothingModeAntiAlias); g.Clear(Color(0, 0, 0, 0));
    float s = (float)size;
    GraphicsPath sq; RR(sq, 0.5f, 0.5f, s - 1, s - 1, s * 0.24f);
    LinearGradientBrush grad(PointF(0, 0), PointF(s, s), Color(255, 196, 181, 253), Color(255, 124, 58, 237));
    g.FillPath(&grad, &sq);
    PointF bolt[] = {{s * 0.58f, s * 0.14f}, {s * 0.28f, s * 0.56f}, {s * 0.47f, s * 0.56f}, {s * 0.40f, s * 0.86f}, {s * 0.73f, s * 0.42f}, {s * 0.53f, s * 0.42f}};
    SolidBrush white(Color(255, 255, 255, 255)); g.FillPolygon(&white, bolt, 6);
    bmp.GetHICON(&icon);
    return icon;
}

static int HitAt(float x, float y) {
    for (int i = (int)gHits.size() - 1; i >= 0; i--) if (gHits[i].r.Contains(x, y)) return gHits[i].id;
    return -1;
}
// window-local logical point (origin at the window's top-left corner, shadow margin removed)
static void ToLogical(LPARAM l, float* x, float* y) { *x = (short)LOWORD(l) / gScale - M; *y = (short)HIWORD(l) / gScale - M; }
static int HitAtWindow(float x, float y) {
    int t = TitleHit(x, y);
    if (t >= 0) return t;
    return y < TB ? -1 : HitAt(x, y - TB);
}

static void StartRunning() {
    int nTicked = 0; for (auto& a : gApps) nTicked += a.ticked;
    if (nTicked == 0 || gClickAt >= 0) return;
    gClickAt = gNow;
    int k = 0; float last = 0;
    for (auto& a : gApps) {
        if (!a.ticked) continue;
        a.closeAt = gClickAt + 750 + 900.0f * k + (a.leftRunning ? 1200.0f : 0);
        last = std::max(last, a.closeAt);
        k++;
    }
    gDoneAt = last + 700;
    gFocus = -1;
}

static void Activate(int id, HWND hw) {
    if (id >= 0 && id <= 4) { if (gClickAt < 0) gApps[id].ticked = !gApps[id].ticked; }
    else if (id == 6) StartRunning();
    else if (id == 5) DestroyWindow(hw);
    else if (id == 7) DestroyWindow(hw);
    else if (id == 8) ShowWindow(hw, SW_MINIMIZE);
    else if (id == 9) DestroyWindow(hw);
}

static void Replay() {
    ResetApps();
    gClickAt = gDoneAt = -1;
    QueryPerformanceCounter(&gStartQpc);
    gFocus = 6;
}

// One frame: draw everything into a per-pixel-alpha surface and hand it to Windows in a single call, so the
// size, the position and the pixels of the window all change together (no tearing, no resize flicker).
static void Render() {
    if (!gWnd || IsIconic(gWnd)) return;
    gNow = NowMs();
    Lay L = Layout(gNow);
    gBody = L.body;
    float totW = W + 2 * M, totH = TB + gBody + 2 * M;
    int cw = (int)std::lround(totW * gScale), ch = (int)std::lround(totH * gScale);
    cw = std::min(cw, gMaxW); ch = std::min(ch, gMaxH);

    {
        Graphics g(gSurface);
        g.SetCompositingMode(CompositingModeSourceCopy);
        SolidBrush clear(Color(0, 0, 0, 0));
        g.FillRectangle(&clear, 0, 0, cw, ch);
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.ScaleTransform(gScale, gScale);
        float H = TB + gBody;
        GraphicsPath win; RR(win, M, M, W, H, 14);
        {   // soft shadow, drawn only in the ring outside the window (the inside is covered anyway)
            Region ring(RectF(0, 0, totW, totH));
            ring.Exclude(&win);
            g.SetClip(&ring);
            for (int i = 0; i < 7; i++) {
                float e = (float)i * 3.2f;
                FillRR(g, Color((BYTE)(10 + 1.6f * (7 - i)), 0, 0, 0), M - e, M + 5 - e, W + 2 * e, H + 2 * e, 14 + e);
            }
            g.ResetClip();
        }
        g.SetClip(&win);
        g.TranslateTransform(M, M + TB);
        Frame(g, gNow);
        g.ResetTransform();
        g.ScaleTransform(gScale, gScale);
        g.TranslateTransform(M, M);
        DrawTitle(g, GetForegroundWindow() == gWnd);
        g.ResetClip();
        g.ResetTransform();
        g.ScaleTransform(gScale, gScale);
        StrokeRR(g, Color(255, 44, 37, 58), 1.0f, M + 0.5f, M + 0.5f, W - 1, H - 1, 14);
    }
    GdiFlush();

    // keep the window centred where it is, even while its height changes; follow it if the user drags it
    RECT cur; GetWindowRect(gWnd, &cur);
    if (gLastL != -99999 && (cur.left != gLastL || cur.top != gLastT)) { gCx = cur.left + (cur.right - cur.left) / 2.0; gCy = cur.top + (cur.bottom - cur.top) / 2.0; }
    int left = (int)std::lround(gCx - cw / 2.0), top = (int)std::lround(gCy - ch / 2.0);
    gLastL = left; gLastT = top;
    POINT dst = {left, top}, src = {0, 0};
    SIZE sz = {cw, ch};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(gWnd, nullptr, &dst, &sz, gMemDC, &src, 0, &bf, ULW_ALPHA);
}

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
        case WM_TIMER:  // fallback while Windows runs its own loop (dragging the window)
            if (NowMs() - gLastRender > 12) { Render(); gLastRender = NowMs(); }
            return 0;
        case WM_NCHITTEST: {
            POINT pt = {(short)LOWORD(l), (short)HIWORD(l)};
            RECT r; GetWindowRect(h, &r);
            float x = (pt.x - r.left) / gScale - M, y = (pt.y - r.top) / gScale - M;
            float H = TB + gBody;
            if (x < 0 || y < 0 || x >= W || y >= H) return HTTRANSPARENT;  // the shadow margin
            if (y < TB && TitleHit(x, y) < 0) return HTCAPTION;             // drag by the strip
            return HTCLIENT;
        }
        case WM_NCACTIVATE: return TRUE;
        case WM_MOUSEWHEEL: gScrollTarget -= (short)HIWORD(w) / 120.0f * 60.0f; return 0;
        case WM_DPICHANGED: {
            WCHAR e[8] = {};
            if (!GetEnvironmentVariableW(L"GMT_SCALE", e, 8)) ApplyScale(h, HIWORD(w) / 96.0f);   // keeps the centre where it is
            return 0;
        }
        case WM_MOUSEMOVE: {
            float mx, my; ToLogical(l, &mx, &my);
            gTitleHover = TitleHit(mx, my);
            gHover = gTitleHover >= 0 ? -1 : (my < TB ? -1 : HitAt(mx, my - TB));
            TRACKMOUSEEVENT te = {sizeof(te), TME_LEAVE, h, 0}; TrackMouseEvent(&te);
            return 0;
        }
        case WM_MOUSELEAVE: gHover = -1; gTitleHover = -1; return 0;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, (gHover >= 0 || gTitleHover >= 0) ? IDC_HAND : IDC_ARROW)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            float mx, my; ToLogical(l, &mx, &my);
            gPressed = HitAtWindow(mx, my);
            if (gPressed >= 0 && gPressed < 8) gFocus = gPressed;
            SetCapture(h);
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            float mx, my; ToLogical(l, &mx, &my);
            int id = HitAtWindow(mx, my);
            int pressed = gPressed; gPressed = -1;
            if (id >= 0 && id == pressed) Activate(id, h);
            return 0;
        }
        case WM_KEYDOWN: {
            bool review = gClickAt < 0, done = gDoneAt >= 0 && gNow > gDoneAt + T_P2;
            if (w == 'R') { Replay(); return 0; }
            if (w == VK_ESCAPE) { if (review || done) DestroyWindow(h); return 0; }
            if (review) {
                const int order[] = {0, 1, 2, 3, 4, 5, 6};
                int idx = 6; for (int i = 0; i < 7; i++) if (order[i] == gFocus) idx = i;
                bool shift = GetKeyState(VK_SHIFT) < 0;
                if (w == VK_TAB || w == VK_DOWN || w == VK_UP || w == VK_RIGHT || w == VK_LEFT) {
                    bool back = (w == VK_TAB && shift) || w == VK_UP || w == VK_LEFT;
                    idx = (idx + (back ? 6 : 1)) % 7; gFocus = order[idx];
                } else if (w == VK_SPACE) { if (gFocus >= 0 && gFocus <= 4) gApps[gFocus].ticked = !gApps[gFocus].ticked; else if (gFocus == 5 || gFocus == 6) Activate(gFocus, h); }
                else if (w == VK_RETURN) { if (gFocus == 5) Activate(5, h); else StartRunning(); }
            } else if (done && (w == VK_RETURN || w == VK_SPACE)) DestroyWindow(h);
            return 0;
        }
        case WM_DESTROY: gWnd = nullptr; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- scale, monitor and surface -------------------------------------------------------------------------------

// The DPI of the monitor the window is on; GMT_SCALE overrides it for testing. Falls back for older Windows.
static float ScaleFor(HWND h) {
    WCHAR e[32] = {};
    if (GetEnvironmentVariableW(L"GMT_SCALE", e, 32) && e[0]) return std::clamp((float)_wtof(e), 1.0f, 4.0f);
    using GetDpiForWindow_t = UINT(WINAPI*)(HWND);
    static auto fn = (GetDpiForWindow_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (fn) { UINT d = fn(h); if (d) return d / 96.0f; }
    HDC dc = GetDC(nullptr); int d = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc);
    return d > 0 ? d / 96.0f : 1.0f;
}

// How many rows fit: the window may use at most ~92% of the monitor's work area; longer lists scroll.
static void ComputeListCap(HWND h) {
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    float availLogical = (mi.rcWork.bottom - mi.rcWork.top) / gScale;
    float cap = availLogical * 0.92f - 2 * M - TB - y0 - 92;
    gListCap = std::clamp(cap, 3 * (rowH + gap), 9 * (rowH + gap));
}

static void AllocSurface() {
    delete gSurface; gSurface = nullptr;
    if (gDib) { DeleteObject(gDib); gDib = nullptr; }
    if (gMemDC) { DeleteDC(gMemDC); gMemDC = nullptr; }
    gMaxW = (int)std::ceil((W + 2 * M) * gScale) + 2;
    gMaxH = (int)std::ceil((TB + y0 + std::max(gListCap, 300.0f) + 100 + 2 * M) * gScale) + 2;
    HDC screen = GetDC(nullptr);
    gMemDC = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = gMaxW; bi.bmiHeader.biHeight = -gMaxH;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    gDib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(gMemDC, gDib);
    ReleaseDC(nullptr, screen);
    gSurface = new Bitmap(gMaxW, gMaxH, gMaxW * 4, PixelFormat32bppPARGB, (BYTE*)bits);
}

// Everything that depends on the scale: baked glows, icons, the drawing surface.
static void ApplyScale(HWND h, float scale) {
    gScale = scale;
    for (auto& b : gGlowBmp) { delete b; b = nullptr; }
    delete gBg; gBg = nullptr; gBgKey = -1;
    ComputeListCap(h);
    AllocSurface();
    if (gIconS) DestroyIcon(gIconS);
    if (gIconB) DestroyIcon(gIconB);
    gIconS = MakeIcon(std::max(16, (int)(16 * gScale))); gIconB = MakeIcon(std::max(32, (int)(32 * gScale)));
    SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)gIconS); SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)gIconB);
}

static void MakeManyApps(int n) {
    static const wchar_t* names[] = {L"Discord", L"Spotify", L"WhatsApp", L"Dropbox", L"Telegram", L"Slack", L"OneDrive", L"Steam Web Helper",
                                     L"Microsoft Teams (work or school account)", L"Adobe Creative Cloud Desktop Service Host",
                                     L"Notion", L"Zoom"};
    static Color cols[] = {Color(255, 88, 101, 242), Color(255, 29, 185, 84), Color(255, 37, 211, 102), Color(255, 0, 97, 255), Color(255, 42, 171, 238), Color(255, 224, 30, 90), Color(255, 255, 140, 0)};
    gApps.clear();
    static std::vector<std::wstring> keep;   // keeps the numbered names alive
    keep.clear(); keep.reserve(n);
    for (int i = 0; i < n; i++) {
        std::wstring nm = names[i % 12];
        if (i >= 12) nm += L" " + std::to_wstring(i / 12 + 1);
        keep.push_back(nm);
    }
    for (int i = 0; i < n; i++) gApps.push_back({keep[i].c_str(), cols[i % 7], (i % 3 == 2) ? L"background" : L"", i != 4, i == 3, 0});
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) gFont = argv[1];
    {   // per-monitor DPI awareness where Windows has it, system awareness otherwise
        using SetCtx_t = BOOL(WINAPI*)(HANDLE);
        auto setCtx = (SetCtx_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
        if (!(setCtx && setCtx((HANDLE)-4))) SetProcessDPIAware();
    }
    GdiplusStartupInput gi; GdiplusStartup(&gTok, &gi, nullptr);
    QueryPerformanceFrequency(&gFreq); QueryPerformanceCounter(&gStartQpc);
    ResetApps();
    WCHAR many[16] = {};
    if (GetEnvironmentVariableW(L"GMT_APPS", many, 16) && _wtoi(many) > 0) MakeManyApps(std::min(_wtoi(many), 200));
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = Proc; wc.hInstance = inst; wc.lpszClassName = L"GmFlowDemo"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // open on the monitor the mouse is on, centred in its work area
    POINT pt; GetCursorPos(&pt);
    MONITORINFO mi = {sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    gCx = (mi.rcWork.left + mi.rcWork.right) / 2.0;
    gCy = (mi.rcWork.top + mi.rcWork.bottom) / 2.0;
    gWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW | WS_EX_TOPMOST, L"GmFlowDemo", L"Game Mode", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                           (int)gCx - 100, (int)gCy - 100, 200, 200, nullptr, nullptr, inst, nullptr);  // on that monitor, so its DPI is the right one
    ApplyScale(gWnd, ScaleFor(gWnd));

    // Warm up first: the first frame loads the font and bakes the glows, which can take a while. Only then start
    // the flow clock and show the window, so the opening animation plays in full instead of being skipped.
    Render();
    Render();
    QueryPerformanceCounter(&gStartQpc);
    gNow = 0;
    Render();
    ShowWindow(gWnd, SW_SHOWNA);
    SetForegroundWindow(gWnd);
    SetTimer(gWnd, 1, 16, nullptr);

    // main loop: handle input, draw a frame, wait for the monitor's next refresh
    MSG msg;
    bool run = true;
    bool bench = GetEnvironmentVariableW(L"GMT_BENCH", nullptr, 0) > 0;
    std::vector<float> frames, renders;
    std::vector<std::pair<float, float>> spikes;
    while (run) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { run = false; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!run) break;
        float r0 = NowMs();
        Render();
        if (bench) renders.push_back(NowMs() - r0);
        gLastRender = NowMs();
        if (FAILED(DwmFlush())) Sleep(8);
        if (bench) { static float prev = 0; float n = NowMs(); if (prev > 0) { frames.push_back(n - prev); if (n - prev > 25) spikes.push_back({n, n - prev}); } prev = n; }
    }
    if (bench && !frames.empty()) {   // GMT_BENCH=1: write frame timing statistics
        std::vector<float> v = frames; std::sort(v.begin(), v.end());
        double sum = 0; for (float f : v) sum += f;
        WCHAR tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring path = std::wstring(tmp) + L"flow-bench.txt";
        FILE* fp = _wfopen(path.c_str(), L"w");
        if (fp) {
            int over = 0; for (float f : v) over += f > 20.0f;
            std::vector<float> rv = renders; std::sort(rv.begin(), rv.end());
            double rs = 0; for (float f : rv) rs += f;
            fprintf(fp, "frames=%zu avg=%.2fms p50=%.2f p95=%.2f p99=%.2f max=%.2f over20ms=%d | draw avg=%.2fms p95=%.2f max=%.2f scale=%.2f\n", v.size(), sum / v.size(), v[v.size() / 2], v[(size_t)(v.size() * 0.95)], v[(size_t)(v.size() * 0.99)], v.back(), over, rs / rv.size(), rv[(size_t)(rv.size() * 0.95)], rv.back(), gScale);
            for (auto& sp : spikes) fprintf(fp, "spike at %.0f ms: frame took %.0f ms\n", sp.first, sp.second);
            fclose(fp);
        }
    }
    GdiplusShutdown(gTok);
    return 0;
}
