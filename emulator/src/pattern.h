#pragma once

// Fixed-size step-pattern storage for the M2a+ sequencer.
// 4 tracks x up to 256 steps (static storage, no dynamic allocation).
// Pattern length is GLOBAL (all tracks share it), 1..256, default 16.
// Track→engine mapping:
//   T1 → yawn track 0 (SubtractiveSynth), pitch = t1Note (+ per-step)
//   T2 → yawn track 1 (DrumRack), pad note 36 (kick)
//   T3 → yawn track 1 (DrumRack), pad note 38 (hat)
//   T4 → yawn track 1 (DrumRack), pad note 39 (clap)

#include <cstdint>

namespace gb {

struct Pattern {
    static constexpr int kTracks = 4;
    static constexpr int kMaxSteps = 256; // paged by 16 in the UI
    static constexpr int kPageSteps = 16;

    struct Step {
        bool on = false;
        bool accent = false;   // accent forces velocity 127
        uint8_t vel = 100;     // slider value at entry time (1..127)
        // Per-step pitch (T1 only): offset in semitones from the track
        // default note (t1Note). 0 = track default. Drums ignore it.
        int8_t noteOffset = 0;
        // Per-step gate (T1 only): 0 = inherit t1Gate, else 50..100 %.
        uint8_t gate = 0;
    };

    Step steps[kTracks][kMaxSteps];

    // Global pattern length in steps (1..kMaxSteps). All tracks share
    // it; the scheduler wraps at this boundary.
    int length = 16;

    // T1 melodic note (MIDI note number) and its gate length (fraction
    // of a 16th step); adjusted by encoders 1/2 on the SEQ page when no
    // step is held (with a held step they edit the step instead).
    uint8_t t1Note = 60;       // C4
    float t1Gate = 0.7f;

    // Drum pad note per track (T2-4); T1 is melodic and uses t1Note.
    static constexpr int kPadNote[kTracks] = {0, 36, 38, 39};

    // yawn engine track index for a panel track (0-based).
    // M2d: one engine track per UI track (T1-T4 → 0-3).
    static int engineTrack(int track) { return track; }

    // MIDI note a panel track plays live (play mode) / track default.
    int noteForTrack(int track) const {
        return track == 0 ? t1Note : kPadNote[track];
    }

    // MIDI note for a specific step (per-step pitch on T1).
    int noteForStep(int track, int step) const {
        int n = noteForTrack(track);
        if (track == 0) n += steps[0][step].noteOffset;
        return n;
    }

    // Gate (fraction of a 16th) for a specific step.
    float gateForStep(int track, int step) const {
        if (track == 0 && steps[0][step].gate > 0)
            return steps[0][step].gate / 100.0f;
        return track == 0 ? t1Gate : 0.7f;
    }

    int pageCount() const { return (length + kPageSteps - 1) / kPageSteps; }

    void clearTrack(int t) {
        for (int s = 0; s < kMaxSteps; ++s) steps[t][s] = Step{};
    }

    void fillFourFloor(int t) {
        for (int s = 0; s < length; ++s) {
            steps[t][s].on = (s % 4 == 0);
            steps[t][s].accent = (s % 4 == 0);
            if (steps[t][s].on) steps[t][s].vel = 120;
        }
    }
};

} // namespace gb
