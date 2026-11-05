#include <windows.h>
#include <d3d10.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "screenshot.h"

namespace {

uint32_t g_crcTable[256];

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc)
{
    if (!g_crcTable[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            g_crcTable[i] = c;
        }
    }
    for (size_t i = 0; i < n; i++) crc = g_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void PutBE32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void WriteChunk(FILE* f, const char type[4], const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> head;
    PutBE32(head, (uint32_t)data.size());
    fwrite(head.data(), 1, 4, f);
    fwrite(type, 1, 4, f);
    if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
    uint32_t crc = Crc32((const uint8_t*)type, 4, 0xFFFFFFFF);
    crc = Crc32(data.data(), data.size(), crc) ^ 0xFFFFFFFF;
    std::vector<uint8_t> tail;
    PutBE32(tail, crc);
    fwrite(tail.data(), 1, 4, f);
}

// PNG with uncompressed ("stored") deflate blocks: no zlib dependency needed.
bool WritePng(const char* path, int w, int h, const std::vector<uint8_t>& rgb)
{
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
    }

    std::vector<uint8_t> z = { 0x78, 0x01 };
    for (size_t pos = 0; pos < raw.size() || raw.empty();) {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    PutBE32(z, (b << 16) | a);

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") || !f) return false;
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    PutBE32(ihdr, w);
    PutBE32(ihdr, h);
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 }); // 8-bit RGB
    WriteChunk(f, "IHDR", ihdr);
    WriteChunk(f, "IDAT", z);
    WriteChunk(f, "IEND", {});
    fclose(f);
    return true;
}

} // namespace

bool SaveScreenshot(IDXGISwapChain* swapChain, const char* path, const char** error)
{
    ID3D10Device* device = nullptr;
    ID3D10Texture2D* backBuffer = nullptr;
    ID3D10Texture2D* staging = nullptr;
    bool ok = false;
    *error = nullptr;

    do {
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D10Device), (void**)&device))) { *error = "GetDevice failed"; break; }
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)&backBuffer))) { *error = "GetBuffer failed"; break; }

        D3D10_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);
        bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if ((!bgra && !rgba) || desc.SampleDesc.Count != 1) { *error = "unsupported back buffer format"; break; }

        desc.Usage = D3D10_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) { *error = "staging texture failed"; break; }
        device->CopyResource(staging, backBuffer);

        D3D10_MAPPED_TEXTURE2D mapped;
        if (FAILED(staging->Map(0, D3D10_MAP_READ, 0, &mapped))) { *error = "Map failed"; break; }

        // 2x2 box downscale keeps files small enough to view quickly.
        int w = (int)desc.Width / 2, h = (int)desc.Height / 2;
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        int r = bgra ? 2 : 0, bch = bgra ? 0 : 2;
        for (int y = 0; y < h; y++) {
            const uint8_t* row0 = (const uint8_t*)mapped.pData + (size_t)(y * 2) * mapped.RowPitch;
            const uint8_t* row1 = row0 + mapped.RowPitch;
            uint8_t* out = &rgb[(size_t)y * w * 3];
            for (int x = 0; x < w; x++) {
                const uint8_t* p[4] = { row0 + x * 8, row0 + x * 8 + 4, row1 + x * 8, row1 + x * 8 + 4 };
                out[x * 3 + 0] = uint8_t((p[0][r] + p[1][r] + p[2][r] + p[3][r]) / 4);
                out[x * 3 + 1] = uint8_t((p[0][1] + p[1][1] + p[2][1] + p[3][1]) / 4);
                out[x * 3 + 2] = uint8_t((p[0][bch] + p[1][bch] + p[2][bch] + p[3][bch]) / 4);
            }
        }
        staging->Unmap(0);

        if (!WritePng(path, w, h, rgb)) { *error = "could not write file"; break; }
        ok = true;
    } while (false);

    if (staging) staging->Release();
    if (backBuffer) backBuffer->Release();
    if (device) device->Release();
    return ok;
}
