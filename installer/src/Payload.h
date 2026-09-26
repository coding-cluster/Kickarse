// Reads the embedded payload (VST3 bundle tree + VST2 dll + CLAP file + license/readme) out of
// this executable's own resources. The payload is produced at build time by
// installer/tools/PackPayload.ps1 into two flat blobs embedded as RCDATA:
//   IDR_PAYLOAD_DATA     -- raw file bytes, back to back
//   IDR_PAYLOAD_MANIFEST -- a tiny custom table describing where each file's bytes are
//
// manifest.bin wire format (all little-endian, written by PackPayload.ps1):
//   char[4]  magic "KKPK"
//   uint32   version (=1)
//   uint32   entryCount
//   entryCount times:
//     uint8    category (0=VST3, 1=VST2, 2=CLAP, 3=DOC)
//     uint8[3] padding
//     uint32   relPathByteLen
//     uint8[relPathByteLen] relPath, UTF-8, backslash-separated
//     uint64   offset   (into payload.bin)
//     uint64   size
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace kick {

enum class PayloadCategory : uint8_t { VST3 = 0, VST2 = 1, CLAP = 2, DOC = 3 };

struct PayloadEntry {
    PayloadCategory category;
    std::wstring relPath; // relative to the category's install root, backslash-separated
    uint64_t offset;
    uint64_t size;
};

struct PayloadBlob {
    const uint8_t* data;
    size_t size;
};

// Throws std::runtime_error with a human-readable message on any failure (missing/corrupt
// resource -- should never happen for a properly built installer, but we don't want to crash).
std::vector<PayloadEntry> LoadPayloadManifest();

// Pointer stays valid for the process lifetime (backed by the module's mapped resource data).
PayloadBlob GetPayloadBlob();

} // namespace kick
