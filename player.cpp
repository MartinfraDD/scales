#include <opxy/player.hpp>

// Scales keeps a track in one key. A note already in the scale plays as it is.
// A note outside it moves to a scale note, stays silent, or, with the white
// keys fit, the white keys play the scale in order and the black keys rest.
// A chord setting stacks scale thirds on whatever a note becomes. With an arp
// rate set, the held notes play one at a time instead of together.
namespace scales {
using opxy::u8;
using opxy::u16;
using opxy::u32;
using opxy::i32;
using opxy::player::Controls;
using opxy::player::Display;
using opxy::player::Plan;
using opxy::player::Tone;
using opxy::player::Update;

enum Fit : u8 { kNearest, kDown, kUp, kMute, kWhiteKeys, kFitCount };
enum Chord : u8 { kSingle, kThird, kTriad, kSeventh, kChordCount };
enum Direction : u8 { kUpward, kDownward, kUpDown, kUpDownRepeat, kRandom, kAsPlayed, kDirectionCount };
enum Length : u8 { kStaccato, kNormal, kLegato, kLengthCount };

const u32 kPitchClasses = 12;
const u32 kMaxTones = 4;
const u32 kMaxRoots = 8;
const u32 kRateCount = 10;  // off, then the stock arpeggio's 1/4 down to 1/64
const u32 kOctaves = 4;

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
    u8 arp;      // feeds the arp instead of sounding
};

// The arp sounds one note at a time on the record of the key that started it,
// replacing that record's tone as each note begins and ends.
struct Arp {
    u8 running;
    u8 carrier;  // the key whose record holds the arp's note
    u8 stop;     // the carrier is due silence
    u16 rest;    // steps from the sounding note's end to the next note
    u32 wait;    // steps until the note ends or, in a rest, the next begins
    u32 position;  // notes played since the arp started
    Tone tone;   // the note sounding
};

struct State {
    Root roots[kMaxRoots];  // in the order their keys went down
    u32 silent[4];  // keys held without sounding, one bit per note
    Arp arp;
    u32 seed;
    u8 input;  // a press or release came since the last update
    u8 calls;  // updates the SDK has taken in a row
};

namespace {
const char* const kKeyNames[kPitchClasses] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kFitCaptions[kFitCount] = {"near", "down", "up", "mute", "white"};
const char* const kChordCaptions[kChordCount] = {"off", "3rd", "triad", "7th"};
const char* const kRateCaptions[kRateCount] = {"off", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T",
                                               "1/32", "1/32T", "1/64"};
const char* const kDirectionCaptions[kDirectionCount] = {"up", "down", "up/dn", "u/r/d", "rand", "order"};
const char* const kLengthCaptions[kLengthCount] = {"stac", "norm", "leg"};
const char* const kRangeCaptions[kOctaves] = {"1 oct", "2 oct", "3 oct", "4 oct"};
const u8 kChordTones[kChordCount] = {1, 2, 3, 4};
// Player steps between arp notes. Stock steps a player 240 times a quarter
// note at the project tempo.
const u8 kPeriods[kRateCount] = {0, 240, 160, 120, 80, 60, 40, 30, 20, 15};
// The share of its period a note sounds, in percent.
const u8 kGates[kLengthCount] = {25, 55, 100};
// A delay no rest outlasts. A tone queued this far ahead keeps the arp's
// record between notes.
const u16 kQueued = 0xFFFF;
const u32 kDrain = 8;  // updates the SDK takes after a press, a release or a step
const i32 kWhiteIndex[kPitchClasses] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};

struct Settings {
    u32 key;
    const Scale* scale;
    Fit fit;
    Chord chord;
    u32 rate;  // zero is off
    Direction direction;
    Length length;
    u32 range;  // octaves
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
    s.rate = choice(controls.value[4], kRateCount);
    s.direction = static_cast<Direction>(choice(controls.value[5], kDirectionCount));
    s.length = static_cast<Length>(choice(controls.value[6], kLengthCount));
    s.range = 1 + choice(controls.value[7], kOctaves);
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

void drop(State& state, Root* root) {
    Root* const last = state.roots + kMaxRoots - 1;
    for (; root < last; ++root) *root = root[1];
    *last = Root{};
}

bool arpeggiated(const State& state) {
    for (const auto& root : state.roots)
        if (root.note && root.arp) return true;
    return false;
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

// The notes the arp walks: each pitch its keys hold, once, low to high unless
// the keys' own order is wanted.
u32 gather(const State& state, bool sorted, Tone* pool) {
    u32 count = 0;
    for (const auto& root : state.roots) {
        if (!root.note || !root.arp) continue;
        for (u32 i = 0; i < root.count; ++i) {
            const u8 pitch = root.pitch[i];
            bool known = false;
            for (u32 j = 0; j < count; ++j) known |= pool[j].pitch == pitch;
            if (known) continue;
            u32 at = count++;
            for (; sorted && at && pool[at - 1].pitch > pitch; --at) pool[at] = pool[at - 1];
            pool[at] = Tone{pitch, root.velocity, 0};
        }
    }
    return count;
}

// Entry `wanted` of the pool repeated up the range's octaves, and how many
// entries there are. Pitches past MIDI 127 are left out.
u32 spread(const Tone* pool, u32 count, u32 range, u32 wanted, Tone& out) {
    u32 n = 0;
    for (u32 octave = 0; octave < range; ++octave)
        for (u32 i = 0; i < count; ++i) {
            const u32 pitch = pool[i].pitch + 12 * octave;
            if (pitch > 127) continue;
            if (n++ == wanted) out = Tone{static_cast<u8>(pitch), pool[i].velocity, 0};
        }
    return n;
}

// Which of n notes comes next.
u32 walk(State& state, Direction direction, u32 n) {
    const u32 i = state.arp.position++;
    switch (direction) {
    case kDownward:
        return n - 1 - i % n;
    case kUpDown: {
        const u32 k = i % (n > 1 ? 2 * n - 2 : 1);
        return k < n ? k : 2 * n - 2 - k;
    }
    case kUpDownRepeat: {
        const u32 k = i % (2 * n);
        return k < n ? k : 2 * n - 1 - k;
    }
    case kRandom:
        state.seed = state.seed * 1664525u + 1013904223u;
        return (state.seed >> 16) % n;
    default:
        return i % n;
    }
}

// The arp's next note, from the keys held now.
Tone next_tone(State& state, const Settings& s) {
    Tone pool[kMaxRoots * kMaxTones];
    const u32 count = gather(state, s.direction != kAsPlayed, pool);
    Tone tone = {};
    const u32 n = spread(pool, count, s.range, ~0u, tone);
    spread(pool, count, s.range, walk(state, s.direction, n), tone);
    return tone;
}

// Times a note that begins now: the steps it sounds, with the remainder of its
// period kept as the rest.
u32 pace(Arp& arp, const Settings& s) {
    const u32 steps = kPeriods[s.rate];
    const u32 gate = steps * kGates[s.length] / 100;
    arp.rest = static_cast<u16>(steps - gate);
    return gate;
}

// Starts the arp on the key just pressed, whose plan carries the first note.
void start(State& state, const Settings& s, u32 note, Plan& plan) {
    Arp& arp = state.arp;
    arp = Arp{};
    arp.running = 1;
    arp.carrier = static_cast<u8>(note);
    arp.tone = next_tone(state, s);
    arp.wait = pace(arp, s) + 1;  // the note sounds on the next step
    plan.count = 1;
    plan.tones[0] = arp.tone;
}

void halt(Arp& arp) {
    arp.running = 0;
    arp.stop = 1;
}

// One player step of the arp. True with a change to the carrier's record: the
// end of the sounding note or, once its rest is over, the next note.
bool advance(State& state, const Settings& s, Update& change) {
    Arp& arp = state.arp;
    if (!arp.running) return false;
    if (!s.rate) {
        halt(arp);
        return false;
    }
    if (--arp.wait) return false;
    change.root = arp.carrier;
    change.plan.count = 1;
    Tone& tone = change.plan.tones[0];
    if (arp.rest) {
        // An empty plan would give the record up, so the rest holds a queued tone.
        tone = arp.tone;
        tone.delay = kQueued;
        arp.wait = arp.rest;
        arp.rest = 0;
        return true;
    }
    tone = arp.tone = next_tone(state, s);
    arp.wait = pace(arp, s);
    return true;
}

// A change waiting for stock: silence for the arp's carrier, or a root whose
// pitches a release cut.
bool pending(State& state, Update& change) {
    change.plan.count = 0;
    if (state.arp.stop) {
        state.arp.stop = 0;
        change.root = state.arp.carrier;
        return true;
    }
    for (auto& root : state.roots) {
        if (!root.note || !root.resound) continue;
        root.resound = 0;
        change.root = root.note - 1u;
        plan_for(root, change.plan);
        return true;
    }
    return false;
}
}  // namespace

bool press(State& state, const Controls& controls, u32 note, u32 velocity, Plan& plan) {
    state.input = 1;
    plan.count = 0;
    if (note > 127 || velocity > 127) return true;
    const Settings s = settings(controls);
    Root next = {};
    next.note = static_cast<u8>(note + 1);
    next.velocity = static_cast<u8>(velocity);
    next.count = static_cast<u8>(tones(s, note, next.pitch));
    Root* root = held(state, note);
    if (!next.count) {
        // A sounding root keeps its tones through an empty plan; a fresh key is
        // remembered as silent so its release stops nothing.
        if (!root) set_bit(state.silent, note, true);
        return true;
    }
    // A key feeds the arp or sounds by itself for as long as it is held.
    next.arp = root ? root->arp : s.rate != 0;
    if (!root) root = vacant(state);
    if (!root) return true;  // eight roots already sound; stock drops a ninth as well
    set_bit(state.silent, note, false);
    *root = next;
    Arp& arp = state.arp;
    if (root->arp) {
        if (!arp.running && s.rate) start(state, s, note, plan);
        return true;
    }
    // This plan takes over the record of an arp carried on the same key.
    if (arp.carrier == note) arp.running = 0;
    plan_for(*root, plan);
    return true;
}

bool release(State& state, u32 note) {
    state.input = 1;
    if (note > 127) return true;
    const bool was_silent = test_bit(state.silent, note);
    set_bit(state.silent, note, false);
    Arp& arp = state.arp;
    Root* root = held(state, note);
    // A dropped ninth key must not take the arp's record with it.
    if (!root) return was_silent || (arp.running && arp.carrier == note);
    const Root gone = *root;
    drop(state, root);
    if (gone.arp) {
        // The arp's record outlives its own key until the last arp key is up.
        if (arp.running && !arpeggiated(state)) halt(arp);
        return true;
    }
    // Stock stops the released pitches outright, so any other root still holding
    // one of them is sounded again on the next update.
    for (auto& other : state.roots)
        if (other.note && !other.arp && share_pitch(other, gone)) other.resound = 1;
    return false;
}

bool update(State& state, const Controls& controls, Update& change) {
    // Updates that follow no press or release belong to a player step.
    const bool step = !state.calls && !state.input;
    state.input = 0;
    change.mode = opxy::player::Replace;
    const bool more = (step && advance(state, settings(controls), change)) || pending(state, change);
    state.calls = more && state.calls + 1u < kDrain ? static_cast<u8>(state.calls + 1) : 0;
    return more;
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
    write(shown.values[4], kRateCaptions[s.rate]);
    write(shown.values[5], kDirectionCaptions[s.direction]);
    write(shown.values[6], kLengthCaptions[s.length]);
    write(shown.values[7], kRangeCaptions[s.range - 1]);
}
}  // namespace scales
