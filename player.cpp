#include <opxy/player.hpp>

// Scales keeps a track in one key. A note already in the scale plays as it is.
// A note outside it moves to a scale note, stays silent, or, with the white
// keys fit, the white keys play the scale in order and the black keys rest.
// A chord setting stacks scale thirds on whatever a note becomes.
namespace scales {
using opxy::u8;
using opxy::u32;
using opxy::i32;
using opxy::player::Controls;
using opxy::player::Display;
using opxy::player::Plan;
using opxy::player::Update;

enum Fit : u8 { kNearest, kDown, kUp, kMute, kWhiteKeys, kFitCount };
enum Chord : u8 { kSingle, kThird, kTriad, kSeventh, kChordCount };

const u32 kPitchClasses = 12;
const u32 kMaxTones = 4;
const u32 kMaxRoots = 8;

struct Scale {
    const char* name;
    const char* caption;
    u8 count;
    u8 steps[7];
};

const Scale kScales[] = {
    {"major", "maj", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"minor", "min", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"dorian", "dor", 7, {0, 2, 3, 5, 7, 9, 10}},
    {"phrygian", "phr", 7, {0, 1, 3, 5, 7, 8, 10}},
    {"lydian", "lyd", 7, {0, 2, 4, 6, 7, 9, 11}},
    {"mixolydian", "mix", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"locrian", "loc", 7, {0, 1, 3, 5, 6, 8, 10}},
    {"harmonic minor", "hmin", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"melodic minor", "mmin", 7, {0, 2, 3, 5, 7, 9, 11}},
    {"major pentatonic", "pent", 5, {0, 2, 4, 7, 9}},
    {"minor pentatonic", "mpent", 5, {0, 3, 5, 7, 10}},
    {"blues", "blues", 6, {0, 3, 5, 6, 7, 10}},
};
const u32 kScaleCount = sizeof(kScales) / sizeof(kScales[0]);

struct Root {
    u8 note;  // the key pressed, plus one; zero marks a free slot
    u8 velocity;
    u8 count;
    u8 pitch[kMaxTones];
    u8 resound;  // another root's release cut one of these pitches
};

struct State {
    Root roots[kMaxRoots];
    u32 silent[4];  // keys held without sounding, one bit per note
};

namespace {
const char* const kKeyNames[kPitchClasses] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kFitCaptions[kFitCount] = {"near", "down", "up", "mute", "white"};
const char* const kChordCaptions[kChordCount] = {"off", "3rd", "triad", "7th"};
const u8 kChordTones[kChordCount] = {1, 2, 3, 4};
const i32 kWhiteIndex[kPitchClasses] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};

struct Settings {
    u32 key;
    const Scale* scale;
    Fit fit;
    Chord chord;
    u32 lowest;
};

u32 choice(i32 value, u32 count) {
    const u32 v = value < 0 ? 0u : static_cast<u32>(value) >> 16;
    const u32 index = (v * count) >> 15;
    return index < count ? index : count - 1;
}

Settings settings(const Controls& controls) {
    Settings s;
    s.key = choice(controls.value[0], kPitchClasses);
    s.scale = &kScales[choice(controls.value[1], kScaleCount)];
    s.fit = static_cast<Fit>(choice(controls.value[2], kFitCount));
    s.chord = static_cast<Chord>(choice(controls.value[3], kChordCount));
    s.lowest = controls.lowest_key;
    return s;
}

i32 floor_div(i32 a, i32 b) {
    const i32 q = a / b;
    return a % b < 0 ? q - 1 : q;
}

i32 pitch_class(i32 pitch) {
    return ((pitch % 12) + 12) % 12;
}

bool black(i32 pitch) {
    return kWhiteIndex[pitch_class(pitch)] < 0;
}

i32 white_index(i32 pitch) {
    return floor_div(pitch, 12) * 7 + kWhiteIndex[pitch_class(pitch)];
}

// Where a pitch sits in the scale, or -1 outside it.
i32 step_of(const Settings& s, i32 pitch) {
    const i32 wanted = pitch_class(pitch - static_cast<i32>(s.key));
    const u32 count = s.scale->count;
    for (u32 i = 0; i < count; ++i)
        if (s.scale->steps[i] == wanted) return static_cast<i32>(i);
    return -1;
}

bool in_scale(const Settings& s, i32 pitch) {
    return step_of(s, pitch) >= 0;
}

// Degrees count scale steps from the key's root and carry across octaves, so
// chords and the white-keys fit can walk the scale without thinking in pitches.
i32 degree(const Settings& s, i32 pitch) {
    const i32 above_root = pitch - static_cast<i32>(s.key);
    return floor_div(above_root, 12) * s.scale->count + step_of(s, pitch);
}

i32 pitch_of(const Settings& s, i32 degree) {
    const i32 count = s.scale->count;
    const i32 octave = floor_div(degree, count);
    return static_cast<i32>(s.key) + 12 * octave + s.scale->steps[degree - octave * count];
}

// The scale note a key plays, or -1 when it stays silent.
i32 snap(const Settings& s, i32 note) {
    if (s.fit == kWhiteKeys) {
        if (black(note)) return -1;
        // The lower octave's C plays the root nearest it; the other white keys
        // continue up and down the scale from there.
        const i32 c = static_cast<i32>(s.lowest) + 7;
        const i32 root = c + pitch_class(static_cast<i32>(s.key) + 6) - 6;
        return pitch_of(s, degree(s, root) + white_index(note) - white_index(c));
    }
    if (in_scale(s, note)) return note;
    if (s.fit == kMute) return -1;
    for (i32 d = 1; d < 12; ++d) {
        if (s.fit != kUp && note - d >= 0 && in_scale(s, note - d)) return note - d;
        if (s.fit != kDown && note + d <= 127 && in_scale(s, note + d)) return note + d;
    }
    return -1;
}

u32 tones(const Settings& s, u32 note, u8* pitches) {
    const i32 base = snap(s, static_cast<i32>(note));
    if (base < 0) return 0;
    const i32 first = degree(s, base);
    const u32 wanted = kChordTones[s.chord];
    u32 count = 0;
    for (u32 i = 0; i < wanted; ++i) {
        const i32 pitch = pitch_of(s, first + 2 * static_cast<i32>(i));
        if (pitch >= 0 && pitch <= 127) pitches[count++] = static_cast<u8>(pitch);
    }
    return count;
}

void plan_for(const Root& root, Plan& plan) {
    plan.count = root.count;
    for (u32 i = 0; i < root.count; ++i) {
        plan.tones[i].pitch = root.pitch[i];
        plan.tones[i].velocity = root.velocity;
        plan.tones[i].delay = 0;
    }
}

Root* held(State& state, u32 note) {
    for (auto& root : state.roots)
        if (root.note == note + 1) return &root;
    return nullptr;
}

Root* vacant(State& state) {
    for (auto& root : state.roots)
        if (!root.note) return &root;
    return nullptr;
}

bool share_pitch(const Root& a, const Root& b) {
    for (u32 i = 0; i < a.count; ++i)
        for (u32 j = 0; j < b.count; ++j)
            if (a.pitch[i] == b.pitch[j]) return true;
    return false;
}

bool test_bit(const u32* bits, u32 n) {
    return (bits[n / 32] >> (n % 32)) & 1u;
}

void set_bit(u32* bits, u32 n, bool on) {
    const u32 mask = 1u << (n % 32);
    if (on) bits[n / 32] |= mask;
    else bits[n / 32] &= ~mask;
}

u32 append(char* out, u32 size, u32 at, const char* text) {
    while (*text && at + 1 < size) out[at++] = *text++;
    out[at] = 0;
    return at;
}

template <u32 N> void write(char (&out)[N], const char* text) {
    append(out, N, 0, text);
}
}  // namespace

bool press(State& state, const Controls& controls, u32 note, u32 velocity, Plan& plan) {
    plan.count = 0;
    if (note > 127 || velocity > 127) return true;
    Root next = {};
    next.note = static_cast<u8>(note + 1);
    next.velocity = static_cast<u8>(velocity);
    next.count = static_cast<u8>(tones(settings(controls), note, next.pitch));
    Root* root = held(state, note);
    if (!next.count) {
        // A sounding root keeps its tones through an empty plan; a fresh key is
        // remembered as silent so its release stops nothing.
        if (!root) set_bit(state.silent, note, true);
        return true;
    }
    if (!root) root = vacant(state);
    if (!root) return true;  // eight roots already sound; stock drops a ninth as well
    set_bit(state.silent, note, false);
    *root = next;
    plan_for(*root, plan);
    return true;
}

bool release(State& state, u32 note) {
    if (note > 127) return true;
    const bool was_silent = test_bit(state.silent, note);
    set_bit(state.silent, note, false);
    Root* root = held(state, note);
    if (!root) return was_silent;
    const Root gone = *root;
    *root = Root{};
    // Stock stops the released pitches outright, so any other root still holding
    // one of them is sounded again on the next update.
    for (auto& other : state.roots)
        if (other.note && share_pitch(other, gone)) other.resound = 1;
    return false;
}

bool update(State& state, const Controls&, Update& change) {
    for (auto& root : state.roots) {
        if (!root.note || !root.resound) continue;
        root.resound = 0;
        change.root = root.note - 1u;
        change.mode = opxy::player::Replace;
        plan_for(root, change.plan);
        return true;
    }
    return false;
}

void reset(State& state) {
    state = State{};
}

void display(const State&, const Controls& controls, Display& shown) {
    const Settings s = settings(controls);
    shown.lit = 0;
    for (u32 k = 0; k < opxy::player::display_dots; ++k) {
        const i32 pitch = static_cast<i32>(s.lowest + k);
        const bool plays = pitch <= 127 && (s.fit == kWhiteKeys ? !black(pitch) : in_scale(s, pitch));
        if (plays) shown.lit |= 1u << k;
    }
    u32 at = append(shown.text, sizeof(shown.text), 0, kKeyNames[s.key]);
    at = append(shown.text, sizeof(shown.text), at, " ");
    append(shown.text, sizeof(shown.text), at, s.scale->name);
    write(shown.values[0], kKeyNames[s.key]);
    write(shown.values[1], s.scale->caption);
    write(shown.values[2], kFitCaptions[s.fit]);
    write(shown.values[3], kChordCaptions[s.chord]);
}
}  // namespace scales
