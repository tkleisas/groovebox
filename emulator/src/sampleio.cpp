#include "sampleio.h"

#include <sndfile.h>
#include <algorithm>
#include <filesystem>

namespace gb {

bool writeWavMono(const char* path, const float* data, int frames,
                  int sampleRate) {
    SF_INFO info{};
    info.samplerate = sampleRate;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* sf = sf_open(path, SFM_WRITE, &info);
    if (!sf) return false;
    const sf_count_t written = sf_writef_float(sf, data, frames);
    sf_close(sf);
    return int(written) == frames;
}

int readWavStereo(const char* path, std::vector<float>& out, int& outSr) {
    SF_INFO info{};
    SNDFILE* sf = sf_open(path, SFM_READ, &info);
    if (!sf) return 0;
    outSr = info.samplerate;
    const int frames = int(info.frames);
    const int ch = info.channels;
    std::vector<float> raw(size_t(frames) * ch);
    const sf_count_t got = sf_readf_float(sf, raw.data(), frames);
    sf_close(sf);
    if (got <= 0) return 0;
    const int n = int(got);
    out.resize(size_t(n) * 2);
    if (ch == 1) {
        for (int i = 0; i < n; ++i)
            out[size_t(i) * 2] = out[size_t(i) * 2 + 1] = raw[size_t(i)];
    } else {
        for (int i = 0; i < n; ++i) {
            out[size_t(i) * 2] = raw[size_t(i) * ch];
            out[size_t(i) * 2 + 1] = raw[size_t(i) * ch + 1];
        }
    }
    return n;
}

bool dirExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool listDir(const std::string& path, std::vector<DirEntry>& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) return false;
    out.clear();
    for (const auto& de : std::filesystem::directory_iterator(path, ec)) {
        const auto name = de.path().filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (de.is_directory()) {
            out.push_back({name, true});
        } else if (name.size() > 4 &&
                   (name.rfind(".wav") == name.size() - 4 ||
                    name.rfind(".WAV") == name.size() - 4)) {
            out.push_back({name, false});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const DirEntry& a, const DirEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir > b.isDir;
                  return a.name < b.name;
              });
    return true;
}

} // namespace gb
