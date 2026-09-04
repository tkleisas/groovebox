#pragma once

// WAV read/write (libsndfile, vendored by yawn) + directory listing
// for the SAMPLE/LOAD pages.

#include <string>
#include <vector>

namespace gb {

// Write mono float WAV (48 kHz by convention of the take buffer).
bool writeWavMono(const char* path, const float* data, int frames,
                  int sampleRate);

// Read a WAV into interleaved stereo floats (mono files are
// duplicated to both channels; >2ch files keep the first two).
// Returns frame count (0 = failure); outSr gets the file's rate.
int readWavStereo(const char* path, std::vector<float>& out, int& outSr);

struct DirEntry {
    std::string name;
    bool isDir = false;
};

// Directories first (alphabetical), then *.wav files (alphabetical).
// Returns false if the directory does not exist.
bool listDir(const std::string& path, std::vector<DirEntry>& out);

bool dirExists(const std::string& path);

} // namespace gb
