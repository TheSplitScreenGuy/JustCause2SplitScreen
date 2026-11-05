#include <windows.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include "overlay.h"
#include "splitscreen.h"
#include "game.h"
#include "player2.h"
#include "splitrender.h"
#include "log.h"

namespace {

// Screen-space quad: the vertex shader builds a rectangle (in NDC) from
// SV_VertexID, so no vertex buffer or input layout is needed.
const char kShaderSource[] = R"(
cbuffer Params : register(b0) { float4 rect; float4 color; float4 mode; };
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(rect.xy, rect.zw, uv), 0, 1);
    o.uv = uv * float2(mode.y, 1);
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    if (mode.x > 0.5) {
        float a = tex.Sample(smp, i.uv).r;
        return float4(lerp(color.rgb, float3(1, 1, 1), a), max(color.a, a));
    }
    return color;
}
)";

struct Params {
    float rect[4];
    float color[4];
    float mode[4];
};

const int kTextTexWidth = 1280;
const int kTextTexHeight = 24;
const int kPadding = 6;

struct Resources {
    ID3D10Device* device;
    ID3D10VertexShader* vs;
    ID3D10PixelShader* ps;
    ID3D10Buffer* params;
    ID3D10Texture2D* textTex;
    ID3D10ShaderResourceView* textSrv;
    ID3D10SamplerState* sampler;
    ID3D10BlendState* blend;
    ID3D10RasterizerState* raster;
    ID3D10DepthStencilState* depth;
    ID3D10StateBlock* stateBlock;
};

Resources g_res;
// The player markers' labels ("P2 120 m"), one per half: textures like the status line's.
struct TextSurface {
    ID3D10Texture2D* tex;
    ID3D10ShaderResourceView* srv;
    char last[64];
    int width;
};
TextSurface g_markerLabels[3]; // [0], [1]: the halves' markers; [2]: the join hint
TextSurface& g_hintLabel = g_markerLabels[2];
bool g_createFailed;
bool g_showOverlay = false; // off by default (user, for release); F8 shows it

HDC g_textDC;
HBITMAP g_textBitmap;
HFONT g_font;
void* g_textBits;
char g_lastText[256];
int g_textWidth;

LARGE_INTEGER g_freq, g_fpsStart;
int g_frames;
int g_fps;

template <typename T> void SafeRelease(T*& p)
{
    if (p) { p->Release(); p = nullptr; }
}

void ReleaseResources()
{
    for (TextSurface& t : g_markerLabels) {
        SafeRelease(t.srv);
        SafeRelease(t.tex);
        t.last[0] = '\0';
    }
    SafeRelease(g_res.stateBlock);
    SafeRelease(g_res.depth);
    SafeRelease(g_res.raster);
    SafeRelease(g_res.blend);
    SafeRelease(g_res.sampler);
    SafeRelease(g_res.textSrv);
    SafeRelease(g_res.textTex);
    SafeRelease(g_res.params);
    SafeRelease(g_res.ps);
    SafeRelease(g_res.vs);
    SafeRelease(g_res.device);
    g_lastText[0] = '\0';
}

pD3DCompile LoadCompiler()
{
    const char* names[] = { "d3dcompiler_47.dll", "d3dcompiler_43.dll" };
    for (const char* name : names) {
        if (HMODULE mod = LoadLibraryA(name))
            if (auto fn = (pD3DCompile)GetProcAddress(mod, "D3DCompile"))
                return fn;
    }
    return nullptr;
}

ID3DBlob* Compile(pD3DCompile compile, const char* entry, const char* target)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = compile(kShaderSource, sizeof(kShaderSource) - 1, "jc2ss_overlay", nullptr, nullptr,
                         entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr))
        Log("Shader %s failed: %s", entry, errors ? (const char*)errors->GetBufferPointer() : "unknown error");
    SafeRelease(errors);
    return code;
}

bool CreateGdiText()
{
    if (g_textDC) return true;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = kTextTexWidth;
    bmi.bmiHeader.biHeight = -kTextTexHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_textDC = CreateCompatibleDC(nullptr);
    g_textBitmap = CreateDIBSection(g_textDC, &bmi, DIB_RGB_COLORS, &g_textBits, nullptr, 0);
    g_font = CreateFontA(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, "Consolas");
    if (!g_textDC || !g_textBitmap || !g_font) return false;

    SelectObject(g_textDC, g_textBitmap);
    SelectObject(g_textDC, g_font);
    SetTextColor(g_textDC, RGB(255, 255, 255));
    SetBkColor(g_textDC, RGB(0, 0, 0));
    return true;
}

bool CreateResources(ID3D10Device* device)
{
    g_res.device = device;
    device->AddRef();

    pD3DCompile compile = LoadCompiler();
    if (!compile) { Log("No d3dcompiler DLL found"); return false; }

    ID3DBlob* vsCode = Compile(compile, "VSMain", "vs_4_0");
    ID3DBlob* psCode = Compile(compile, "PSMain", "ps_4_0");
    bool ok = vsCode && psCode &&
              SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &g_res.vs)) &&
              SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), &g_res.ps));
    SafeRelease(vsCode);
    SafeRelease(psCode);
    if (!ok) { Log("Creating overlay shaders failed"); return false; }

    D3D10_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(Params);
    bd.Usage = D3D10_USAGE_DEFAULT;
    bd.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &g_res.params))) return false;

    D3D10_TEXTURE2D_DESC td = {};
    td.Width = kTextTexWidth;
    td.Height = kTextTexHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D10_USAGE_DEFAULT;
    td.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &g_res.textTex))) return false;
    if (FAILED(device->CreateShaderResourceView(g_res.textTex, nullptr, &g_res.textSrv))) return false;
    for (TextSurface& t : g_markerLabels) {
        if (FAILED(device->CreateTexture2D(&td, nullptr, &t.tex))) return false;
        if (FAILED(device->CreateShaderResourceView(t.tex, nullptr, &t.srv))) return false;
    }

    D3D10_SAMPLER_DESC sd = {};
    sd.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D10_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sd, &g_res.sampler))) return false;

    D3D10_BLEND_DESC blend = {};
    blend.BlendEnable[0] = TRUE;
    blend.SrcBlend = D3D10_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D10_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D10_BLEND_ONE;
    blend.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    blend.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blend, &g_res.blend))) return false;

    D3D10_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D10_FILL_SOLID;
    rd.CullMode = D3D10_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rd, &g_res.raster))) return false;

    D3D10_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&dd, &g_res.depth))) return false;

    D3D10_STATE_BLOCK_MASK mask;
    D3D10StateBlockMaskEnableAll(&mask);
    if (FAILED(D3D10CreateStateBlock(device, &mask, &g_res.stateBlock))) return false;

    if (!CreateGdiText()) { Log("Creating GDI text surface failed"); return false; }

    Log("Overlay resources created on device %p", device);
    return true;
}

// Rasterizes a line with GDI into a text texture; returns its width in pixels.
int RasterizeText(ID3D10Texture2D* tex, const char* text)
{
    RECT rc = { 0, 0, kTextTexWidth, kTextTexHeight };
    int len = (int)strlen(text);
    ExtTextOutA(g_textDC, 0, 2, ETO_OPAQUE, &rc, text, len, nullptr);
    GdiFlush();

    SIZE size;
    GetTextExtentPoint32A(g_textDC, text, len, &size);

    // Greyscale GDI output becomes alpha coverage.
    static DWORD pixels[kTextTexWidth * kTextTexHeight];
    const DWORD* src = (const DWORD*)g_textBits;
    for (int i = 0; i < kTextTexWidth * kTextTexHeight; i++) {
        DWORD v = src[i] & 0xFF;
        pixels[i] = v | (v << 8) | (v << 16) | (v << 24);
    }
    g_res.device->UpdateSubresource(tex, 0, nullptr, pixels, kTextTexWidth * 4, 0);
    return size.cx < kTextTexWidth ? (int)size.cx : kTextTexWidth;
}

// Re-rasterizes the status line, only when the text changes.
void UpdateText(const char* text)
{
    if (strcmp(text, g_lastText) == 0) return;
    strcpy_s(g_lastText, text);
    g_textWidth = RasterizeText(g_res.textTex, text);
}

void UpdateLabel(TextSurface& t, const char* text)
{
    if (strcmp(text, t.last) == 0) return;
    strcpy_s(t.last, text);
    t.width = RasterizeText(t.tex, text);
}

void DrawRect(float x, float y, float w, float h, float screenW, float screenH,
              const float color[4], bool textured)
{
    Params p = {};
    p.rect[0] = x / screenW * 2.0f - 1.0f;
    p.rect[1] = 1.0f - y / screenH * 2.0f;
    p.rect[2] = (x + w) / screenW * 2.0f - 1.0f;
    p.rect[3] = 1.0f - (y + h) / screenH * 2.0f;
    memcpy(p.color, color, sizeof(p.color));
    p.mode[0] = textured ? 1.0f : 0.0f;
    p.mode[1] = w / kTextTexWidth; // only sample the used part of the text texture
    g_res.device->UpdateSubresource(g_res.params, 0, nullptr, &p, 0, 0);
    g_res.device->Draw(4, 0);
}

// Each half shows a marker over the other player (user: each player knows where the other one
// is): a dot at his head, a stem, and a label "P2 120 m" in his colour; off the half it sits on the
// half's edge toward him (SplitRenderMarker).
const float kMarkerColors[2][4] = { { 0.20f, 0.55f, 1.00f, 0.85f },   // Player 1: blue
                                    { 1.00f, 0.55f, 0.10f, 0.85f } }; // Player 2: orange
const float kMarkerDot = 10.0f, kMarkerStem = 8.0f, kMarkerMargin = 4.0f;

void DrawPlayerMarkers(float w, float h)
{
    float half = h * 0.5f;
    const float clear[4] = {};
    for (int view = 0; view < 2; view++) {
        float pos[2], distance;
        bool onScreen;
        if (!SplitRenderMarker(view, pos, &distance, &onScreen)) continue;
        int other = 1 - view;
        char text[32];
        sprintf_s(text, "P%d %d m", other + 1, (int)(distance + 0.5f));
        TextSurface& label = g_markerLabels[view];
        UpdateLabel(label, text);
        const float* color = kMarkerColors[other];
        float top = view * half, bottom = top + half;
        float lw = (float)label.width + 2.0f * kMarkerMargin, lh = (float)kTextTexHeight;
        // The dot (kept inside the half), the label above it (below it at the half's top edge).
        float px = pos[0] * w, py = top + pos[1] * half;
        if (px < kMarkerDot) px = kMarkerDot;
        if (px > w - kMarkerDot) px = w - kMarkerDot;
        if (py < top + kMarkerDot) py = top + kMarkerDot;
        if (py > bottom - kMarkerDot) py = bottom - kMarkerDot;
        float lx = px - lw * 0.5f;
        if (lx < kMarkerMargin) lx = kMarkerMargin;
        if (lx > w - lw - kMarkerMargin) lx = w - lw - kMarkerMargin;
        float ly = py - kMarkerDot * 0.5f - kMarkerStem - lh;
        bool below = ly < top + kMarkerMargin;
        if (below) ly = py + kMarkerDot * 0.5f + kMarkerStem;
        DrawRect(px - kMarkerDot * 0.5f, py - kMarkerDot * 0.5f, kMarkerDot, kMarkerDot, w, h, color, false);
        float stemTop = below ? py + kMarkerDot * 0.5f : ly + lh;
        DrawRect(px - 1.0f, stemTop, 2.0f, kMarkerStem, w, h, color, false);
        DrawRect(lx, ly, lw, lh, w, h, color, false);
        g_res.device->PSSetShaderResources(0, 1, &label.srv);
        DrawRect(lx + kMarkerMargin, ly, (float)label.width, lh, w, h, clear, true);
    }
    g_res.device->PSSetShaderResources(0, 1, &g_res.textSrv);
}

// "P2: press Start to join" for a few seconds once the game has loaded in (user: the status
// overlay is hidden at start, so new players need this). Once per launch: after 2 s of gameplay,
// for 8 s of gameplay time (menus pause it), gone as soon as Player 2 joins.
const DWORD kHintDelayMs = 2000, kHintShowMs = 8000;

bool JoinHintVisible()
{
    static DWORD last, played;
    static bool done;
    if (done) return false;
    if (SplitscreenPlayer2Status() == Player2Status::Joined) {
        done = true;
        return false;
    }
    DWORD now = GetTickCount();
    bool gameplay = SplitscreenInGameplay();
    if (gameplay && last) played += now - last > 200 ? 0 : now - last;
    last = gameplay ? now : 0;
    if (played >= kHintDelayMs + kHintShowMs) {
        done = true;
        Log("Join hint: done");
    }
    bool visible = gameplay && played >= kHintDelayMs && !done;
    static bool wasVisible;
    if (visible != wasVisible) {
        wasVisible = visible;
        Log("Join hint %s (played %lu ms, UI mode %d)", visible ? "shown" : "hidden", played, *(volatile int*)0x011958C4);
    }
    return visible;
}

void DrawJoinHint(float w, float h)
{
    UpdateLabel(g_hintLabel, "P2: press Start to join");
    const float bg[4] = { 0.0f, 0.0f, 0.0f, 0.6f };
    const float clear[4] = {};
    float lw = (float)g_hintLabel.width, lh = (float)kTextTexHeight;
    float x = (w - lw) * 0.5f, y = h * 0.78f;
    DrawRect(x - kPadding * 2, y - kPadding, lw + kPadding * 4, lh + kPadding * 2, w, h, bg, false);
    g_res.device->PSSetShaderResources(0, 1, &g_hintLabel.srv);
    DrawRect(x, y, lw, lh, w, h, clear, true);
    g_res.device->PSSetShaderResources(0, 1, &g_res.textSrv);
}

bool KeyPressed(int vk)
{
    static bool down[256];
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = now && !down[vk];
    down[vk] = now;
    return pressed;
}

void PollHotkeys()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    bool focused = pid == GetCurrentProcessId();

    // Always poll so a key held while unfocused does not register on refocus.
    bool f8 = KeyPressed(VK_F8);
    if (focused && f8) g_showOverlay = !g_showOverlay;
    bool f5 = KeyPressed(VK_F5); // F1 is the game's map key
    if (focused && f5) RequestAmmoRefill();
    bool f6 = KeyPressed(VK_F6);
    if (focused && f6) PlayersRequestSwap();
    bool f9 = KeyPressed(VK_F9);
    if (focused && f9) RequestMoney(50000);
}

void UpdateFps()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_freq.QuadPart) {
        QueryPerformanceFrequency(&g_freq);
        g_fpsStart = now;
    }
    g_frames++;
    double elapsed = double(now.QuadPart - g_fpsStart.QuadPart) / double(g_freq.QuadPart);
    if (elapsed >= 0.5) {
        g_fps = int(g_frames / elapsed + 0.5);
        g_frames = 0;
        g_fpsStart = now;
    }
}

} // namespace

void OverlayOnPresent(IDXGISwapChain* swapChain)
{
    UpdateFps();
    PollHotkeys();
    bool splitActive = SplitscreenActive();
    bool hint = JoinHintVisible();
    if (g_createFailed || (!g_showOverlay && !splitActive && !hint)) return;

    ID3D10Device* device = nullptr;
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D10Device), (void**)&device))) return;
    if (device != g_res.device) {
        ReleaseResources();
        if (!CreateResources(device)) {
            Log("Overlay disabled: resource creation failed");
            ReleaseResources();
            g_createFailed = true;
            device->Release();
            return;
        }
    }
    device->Release(); // g_res.device holds its own reference

    ID3D10Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&backBuffer))) return;
    D3D10_TEXTURE2D_DESC bbDesc;
    backBuffer->GetDesc(&bbDesc);

    ID3D10RenderTargetView* rtv = nullptr;
    HRESULT hr = device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    backBuffer->Release();
    if (FAILED(hr)) {
        static bool logged;
        if (!logged) { Log("CreateRenderTargetView failed (hr 0x%08lX, format %d)", hr, bbDesc.Format); logged = true; }
        return;
    }

    static bool loggedBackBuffer;
    if (!loggedBackBuffer) {
        Log("Back buffer %ux%u, format %d, %u samples", bbDesc.Width, bbDesc.Height, bbDesc.Format, bbDesc.SampleDesc.Count);
        loggedBackBuffer = true;
    }

    g_res.stateBlock->Capture();

    float w = (float)bbDesc.Width;
    float h = (float)bbDesc.Height;
    D3D10_VIEWPORT vp = { 0, 0, bbDesc.Width, bbDesc.Height, 0.0f, 1.0f };
    float blendFactor[4] = {};

    device->OMSetRenderTargets(1, &rtv, nullptr);
    device->OMSetBlendState(g_res.blend, blendFactor, 0xFFFFFFFF);
    device->OMSetDepthStencilState(g_res.depth, 0);
    device->RSSetViewports(1, &vp);
    device->RSSetState(g_res.raster);
    device->IASetInputLayout(nullptr);
    device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    device->SOSetTargets(0, nullptr, nullptr);
    device->VSSetShader(g_res.vs);
    device->GSSetShader(nullptr);
    device->PSSetShader(g_res.ps);
    device->VSSetConstantBuffers(0, 1, &g_res.params);
    device->PSSetConstantBuffers(0, 1, &g_res.params);
    device->PSSetShaderResources(0, 1, &g_res.textSrv);
    device->PSSetSamplers(0, 1, &g_res.sampler);

    // Thin black line between the two views (user: replaces the old yellow stand-in).
    if (splitActive) {
        const float divider[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float thickness = h >= 1080.0f ? 3.0f : 2.0f;
        DrawRect(0, h * 0.5f - thickness * 0.5f, w, thickness, w, h, divider, false);
        DrawPlayerMarkers(w, h);
    }
    if (hint) DrawJoinHint(w, h);

    if (g_showOverlay) {
        const char* p2 = "";
        switch (SplitscreenPlayer2Status()) {
        case Player2Status::NoController: p2 = "P2: no controller"; break;
        case Player2Status::Waiting:      p2 = "P2: press Start to join"; break;
        case Player2Status::Joined:       p2 = "P2: joined (Back+Start leaves)"; break;
        }
        char cam[64] = "cam: n/a";
        CameraMatrix m;
        if (ReadMainCamera(&m))
            sprintf_s(cam, "cam %.0f %.0f %.0f", m.position[0], m.position[1], m.position[2]);

        char text[256];
        sprintf_s(text, "JC2 Splitscreen v1.0 |%3d FPS | %s | %s [pad %d] | F5 ammo | F6 swap | F8 hide | F9 $50k",
                  g_fps, cam, p2, SplitscreenPlayer2Slot() + 1);
        UpdateText(text);

        const float bg[4] = { 0.05f, 0.05f, 0.08f, 0.65f };
        const float clear[4] = {};
        DrawRect(10.0f, 10.0f, (float)(g_textWidth + kPadding * 2), (float)(kTextTexHeight + kPadding * 2), w, h, bg, false);
        DrawRect(10.0f + kPadding, 10.0f + kPadding, (float)g_textWidth, (float)kTextTexHeight, w, h, clear, true);
    }

    g_res.stateBlock->Apply();
    rtv->Release();
}
