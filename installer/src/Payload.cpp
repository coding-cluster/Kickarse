#include "Payload.h"

#include <windows.h>
#include <cstring>

extern HINSTANCE g_hInst; // defined in main.cpp

#include "resource.h"

namespace kick {

namespace {

const uint8_t* ReadU8(const uint8_t* p, const uint8_t* end, uint8_t& out) {
    if (p + 1 > end) throw std::runtime_error("payload manifest truncated");
    out = *p;
    return p + 1;
}
const uint8_t* SkipBytes(const uint8_t* p, const uint8_t* end, size_t n) {
    if (p + n > end) throw std::runtime_error("payload manifest truncated");
    return p + n;
}
const uint8_t* ReadU32(const uint8_t* p, const uint8_t* end, uint32_t& out) {
    if (p + 4 > end) throw std::runtime_error("payload manifest truncated");
    memcpy(&out, p, 4);
    return p + 4;
}
const uint8_t* ReadU64(const uint8_t* p, const uint8_t* end, uint64_t& out) {
    if (p + 8 > end) throw std::runtime_error("payload manifest truncated");
    memcpy(&out, p, 8);
    return p + 8;
}

std::wstring Utf8ToWide(const uint8_t* bytes, size_t len) {
    if (len == 0) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes), (int)len, nullptr, 0);
    std::wstring out(wlen, L'\0');
    if (wlen > 0) {
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes), (int)len, &out[0], wlen);
    }
    return out;
}

struct ResourceView {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

ResourceView LoadResourceView(int resId) {
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) throw std::runtime_error("embedded resource missing (installer build is corrupt)");
    HGLOBAL hData = LoadResource(g_hInst, hRes);
    if (!hData) throw std::runtime_error("failed to load embedded resource");
    const void* p = LockResource(hData);
    DWORD sz = SizeofResource(g_hInst, hRes);
    if (!p || sz == 0) throw std::runtime_error("embedded resource is empty");
    ResourceView v;
    v.data = reinterpret_cast<const uint8_t*>(p);
    v.size = sz;
    return v;
}

} // namespace

std::vector<PayloadEntry> LoadPayloadManifest() {
    ResourceView view = LoadResourceView(IDR_PAYLOAD_MANIFEST);
    const uint8_t* p = view.data;
    const uint8_t* end = view.data + view.size;

    if (view.size < 12 || memcmp(p, "KKPK", 4) != 0) {
        throw std::runtime_error("embedded payload manifest has a bad signature");
    }
    p += 4;
    uint32_t version = 0;
    p = ReadU32(p, end, version);
    if (version != 1) throw std::runtime_error("embedded payload manifest has an unsupported version");
    uint32_t count = 0;
    p = ReadU32(p, end, count);

    std::vector<PayloadEntry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t cat = 0;
        p = ReadU8(p, end, cat);
        p = SkipBytes(p, end, 3);
        uint32_t relLen = 0;
        p = ReadU32(p, end, relLen);
        if (p + relLen > end) throw std::runtime_error("embedded payload manifest truncated (path)");
        std::wstring relPath = Utf8ToWide(p, relLen);
        p += relLen;
        uint64_t offset = 0, size = 0;
        p = ReadU64(p, end, offset);
        p = ReadU64(p, end, size);

        PayloadEntry e;
        e.category = static_cast<PayloadCategory>(cat);
        e.relPath = std::move(relPath);
        e.offset = offset;
        e.size = size;
        entries.push_back(std::move(e));
    }
    return entries;
}

PayloadBlob GetPayloadBlob() {
    ResourceView view = LoadResourceView(IDR_PAYLOAD_DATA);
    return PayloadBlob{ view.data, view.size };
}

} // namespace kick
