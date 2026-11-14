#include "../player.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#define require(x) \
    do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

using namespace opxy;
using namespace opxy::player;
using namespace scales;

namespace {
enum Control { kKey, kScale, kFit, kChord, kArp, kDir, kLen, kRange };
// Stock steps a player 240 times a quarter note, so a sixteenth lasts 60 steps.
enum Rate {
    kOff, kQuarter, kQuarterTriplet, kEighth, kEighthTriplet, kSixteenth, kSixteenthTriplet,
    kThirtySecond, kThirtySecondTriplet, kSixtyFourth
};
enum Note { F3 = 53, C4 = 60, Cs4 = 61, D4 = 62, E4 = 64, F4 = 65, G4 = 67, A4 = 69, B4 = 71, C5 = 72 };

const u32 kChoices[8] = {12, kScaleCount, kFitCount, kChordCount, kRateCount, kDirectionCount, kLengthCount, kOctaves};
const u32 kSounding = ~0u;

// The value stock stores for an enum choice: the centre of its bucket.
i32 centre(u32 index, u32 count) {
    return static_cast<i32>(((2ull * index + 1) << 30) / count);
}

struct Rig {
    State state = {};
    // A new track: the definition's defaults.
    Controls controls = {{0, 0, 0, 0, 0, 0, centre(kNormal, kLengthCount), 0}, F3};
    Plan plan = {};
    Update change = {};
    Display shown = {};

    void set(Control control, u32 index) { controls.value[control] = centre(index, kChoices[control]); }
    bool on(u32 note, u32 velocity = 90) { return press(state, controls, note, velocity, plan); }
    bool off(u32 note) { return release(state, note); }
    bool pending() { return update(state, controls, change); }
    const Display& draw() { display(state, controls, shown); return shown; }
};

// The SDK around the player: a record of tones for each sounding root, the
// updates it drains after a press or release and before every step, and
// stock's step, which sounds a tone once its delay has run out.
struct Stage : Rig {
    struct Sound { u32 step; bool on; int pitch; int velocity; };
    struct Voice { Tone tone; bool sounding; };
    struct Record { u32 root; std::vector<Voice> voices; };
    std::vector<Record> records;
    std::vector<Sound> heard;
    u32 now = 0;

    const Record* record(u32 root) const {
        for (const auto& r : records)
            if (r.root == root) return &r;
        return nullptr;
    }
    void stop(u32 root) {
        for (auto r = records.begin(); r != records.end(); ++r) {
            if (r->root != root) continue;
            for (const auto& voice : r->voices)
                if (voice.sounding) heard.push_back({now, false, voice.tone.pitch, voice.tone.velocity});
            records.erase(r);
            return;
        }
    }
    void store(u32 root, const Plan& next, bool existing_only) {
        require(next.count <= max_tones);
        if (existing_only && !record(root)) return;
        stop(root);
        if (!next.count) return;
        require(records.size() < 8);  // stock would drop a ninth
        Record r = {root, {}};
        for (u32 i = 0; i < next.count; ++i) {
            require(next.tones[i].pitch <= 127 && next.tones[i].velocity <= 127);
            r.voices.push_back({next.tones[i], false});
        }
        records.push_back(r);
    }
    void drain() {
        for (u32 i = 0; i < 8 && pending(); ++i) {
            require(change.mode == Replace);
            store(change.root, change.plan, true);
        }
    }
    void down(u32 note, u32 velocity = 90) {
        require(on(note, velocity));
        if (plan.count) store(note, plan, false);
        drain();
    }
    void up(u32 note) {
        if (!off(note)) stop(note);
        drain();
    }
    void run(u32 steps) {
        for (; steps; --steps, ++now) {
            drain();
            for (auto& r : records)
                for (auto& voice : r.voices) {
                    if (voice.sounding) continue;
                    if (voice.tone.delay) { --voice.tone.delay; continue; }
                    voice.sounding = true;
                    heard.push_back({now, true, voice.tone.pitch, voice.tone.velocity});
                }
        }
    }
    // Stock's all notes off.
    void panic() {
        while (!records.empty()) stop(records.back().root);
        reset(state);
    }
};

struct Played { int pitch; int velocity; u32 start; u32 end; };

// The notes heard so far, with the steps each began and ended on.
std::vector<Played> notes(const Stage& s) {
    std::vector<Played> out;
    for (const auto& sound : s.heard) {
        if (sound.on) { out.push_back({sound.pitch, sound.velocity, sound.step, kSounding}); continue; }
        auto note = out.rbegin();
        while (note != out.rend() && (note->pitch != sound.pitch || note->end != kSounding)) ++note;
        require(note != out.rend());
        note->end = sound.step;
    }
    return out;
}

void begins(const Stage& s, std::initializer_list<int> pitches) {
    const auto played = notes(s);
    require(played.size() >= pitches.size());
    unsigned i = 0;
    for (int pitch : pitches) require(played[i++].pitch == pitch);
}

bool quiet(const Stage& s) {
    for (const auto& note : notes(s))
        if (note.end == kSounding) return false;
    return s.records.empty();
}

// Stock holds what the player believes it does: a record for every root that
// sounds by itself, one more carrying the arp while it runs, and nothing else.
void consistent(const Stage& s) {
    u32 expected = 0;
    for (const auto& root : s.state.roots) {
        if (!root.note || root.arp) continue;
        const auto* record = s.record(root.note - 1u);
        require(record && record->voices.size() == root.count);
        ++expected;
    }
    if (s.state.arp.running) {
        const auto* record = s.record(s.state.arp.carrier);
        require(record && record->voices.size() == 1);
        require(arpeggiated(s.state));
        ++expected;
    }
    require(s.records.size() == expected);
}

u32 pitch_mask(const Plan& plan) {
    u32 mask = 0;
    for (u32 i = 0; i < plan.count; ++i) mask |= 1u << (plan.tones[i].pitch % 32);
    return mask;
}

void plays(const Plan& plan, std::initializer_list<int> pitches, int velocity = 90) {
    require(plan.count == pitches.size());
    unsigned i = 0;
    for (int pitch : pitches) {
        require(plan.tones[i].pitch == pitch);
        require(plan.tones[i].velocity == velocity);
        require(plan.tones[i].delay == 0);
        ++i;
    }
}

bool scale_has(u32 key, const Scale& scale, int pitch) {
    const int wanted = ((pitch - static_cast<int>(key)) % 12 + 12) % 12;
    for (u32 i = 0; i < scale.count; ++i)
        if (scale.steps[i] == wanted) return true;
    return false;
}

// Every key, every scale, every note: in-scale notes pass, the rest land on
// the scale note the fit asks for, and nothing lands between.
void fits_cover_every_note() {
    for (u32 key = 0; key < 12; ++key)
        for (u32 scale = 0; scale < kScaleCount; ++scale) {
            const Scale& s = kScales[scale];
            for (int note = 0; note <= 127; ++note) {
                const bool inside = scale_has(key, s, note);
                for (u32 fit = kNearest; fit <= kMute; ++fit) {
                    Rig r;
                    r.set(kKey, key); r.set(kScale, scale); r.set(kFit, fit);
                    require(r.on(note));
                    if (inside) { plays(r.plan, {note}); require(!r.off(note)); continue; }
                    if (fit == kMute) { require(r.plan.count == 0); require(r.off(note)); continue; }
                    if (r.plan.count == 0) {
                        // Only the ends of the range can have no note in the asked direction.
                        require(fit != kNearest);
                        require(fit == kDown ? note < 12 : note > 115);
                        require(r.off(note));
                        continue;
                    }
                    require(r.plan.count == 1);
                    const int played = r.plan.tones[0].pitch;
                    require(scale_has(key, s, played));
                    const int distance = played > note ? played - note : note - played;
                    for (int d = 1; d < distance; ++d) {
                        if (fit != kUp && note - d >= 0) require(!scale_has(key, s, note - d));
                        if (fit != kDown && note + d <= 127) require(!scale_has(key, s, note + d));
                    }
                    if (fit == kDown) require(played < note);
                    if (fit == kUp) require(played > note);
                    if (fit == kNearest && note - distance >= 0 && scale_has(key, s, note - distance)) require(played < note);
                    require(!r.off(note));
                }
            }
        }
}

void nearest_breaks_ties_downward() {
    Rig r;
    const int black[] = {Cs4, 63, 66, 68, 70};
    for (int note : black) { r.on(note); plays(r.plan, {note - 1}); r.off(note); }
    r.set(kFit, kUp);
    for (int note : black) { r.on(note); plays(r.plan, {note + 1}); r.off(note); }
    r.set(kFit, kDown);
    for (int note : black) { r.on(note); plays(r.plan, {note - 1}); r.off(note); }
}

void white_keys_play_the_scale() {
    Rig r;
    r.set(kFit, kWhiteKeys);
    const int whites[] = {C4, D4, E4, F4, G4, A4, B4, C5};
    for (int note : whites) { r.on(note); plays(r.plan, {note}); r.off(note); }
    const int blacks[] = {Cs4, 63, 66, 68, 70};
    for (int note : blacks) { r.on(note); require(r.plan.count == 0); require(r.off(note)); }

    r.set(kKey, 4); r.set(kScale, 1);  // E minor: the root nearest C4 is E4
    const int e_minor[] = {E4, 66, G4, A4, B4, C5, D4 + 12, E4 + 12};
    for (unsigned i = 0; i < 8; ++i) { r.on(whites[i]); plays(r.plan, {e_minor[i]}); r.off(whites[i]); }
    r.on(B4 - 12); plays(r.plan, {D4}); r.off(B4 - 12);  // below C the scale continues downward

    r.set(kKey, 7); r.set(kScale, 0);  // G major: the nearest root is G3, below C4
    const int g_major[] = {G4 - 12, A4 - 12, B4 - 12, C4, D4, E4, 66, G4};
    for (unsigned i = 0; i < 8; ++i) { r.on(whites[i]); plays(r.plan, {g_major[i]}); r.off(whites[i]); }

    r.set(kKey, 0); r.set(kScale, 9);  // major pentatonic: five keys per octave of the scale
    const int pentatonic[] = {C4, D4, E4, G4, A4, C5, D4 + 12, E4 + 12};
    for (unsigned i = 0; i < 8; ++i) { r.on(whites[i]); plays(r.plan, {pentatonic[i]}); r.off(whites[i]); }

    r.set(kScale, 0); r.set(kKey, 4);
    r.controls.lowest_key = F3 + 12;  // the keyboard's lower C moved up an octave with it
    r.on(C5); plays(r.plan, {E4 + 12}); r.off(C5);
    r.on(C4); plays(r.plan, {E4}); r.off(C4);  // a sequenced note below the keyboard follows the same map
}

void chords_stay_in_the_scale() {
    Rig r;
    r.set(kChord, kTriad);
    r.on(C4); plays(r.plan, {C4, E4, G4}); r.off(C4);
    r.on(B4 - 12); plays(r.plan, {B4 - 12, D4, F4}); r.off(B4 - 12);
    r.set(kChord, kThird);
    r.on(E4); plays(r.plan, {E4, G4}); r.off(E4);
    r.set(kChord, kSeventh);
    r.on(G4); plays(r.plan, {G4, B4, D4 + 12, F4 + 12}); r.off(G4);
    r.set(kKey, 4); r.set(kScale, 1); r.set(kChord, kTriad);
    r.on(Cs4); plays(r.plan, {C4, E4, G4}); r.off(Cs4);  // the chord builds on the snapped note
    r.set(kKey, 0); r.set(kScale, 0);
    r.on(124); plays(r.plan, {124, 127}); r.off(124);  // tones above MIDI 127 are dropped
    r.set(kChord, kSingle); r.set(kFit, kDown); r.set(kKey, 2);
    r.on(0); require(r.plan.count == 0); require(r.off(0));  // nothing below C-1 to move down to
}

void released_pitches_sound_again_for_roots_still_holding_them() {
    Rig r;
    r.on(C4, 70); plays(r.plan, {C4}, 70);
    r.on(Cs4, 80); plays(r.plan, {C4}, 80);
    require(!r.pending());
    require(!r.off(C4));
    require(r.pending());
    require(r.change.root == Cs4 && r.change.mode == Replace);
    plays(r.change.plan, {C4}, 80);
    require(!r.pending());
    require(!r.off(Cs4));
    require(!r.pending());

    r.on(Cs4); r.on(C4);
    require(!r.off(Cs4));
    require(r.pending() && r.change.root == C4);
    require(!r.pending());
    require(!r.off(C4));

    r.set(kChord, kTriad);
    r.on(C4); r.on(E4);  // C E G and E G B share two pitches
    require(!r.off(C4));
    require(r.pending() && r.change.root == E4);
    plays(r.change.plan, {E4, G4, B4});
    require(!r.pending());
    require(!r.off(E4));
    require(!r.pending());
}

void held_notes_keep_their_pitches() {
    Rig r;
    r.on(Cs4); plays(r.plan, {C4});
    r.set(kKey, 2);  // D major, where C# belongs
    require(!r.pending());
    require(!r.off(Cs4));
    r.on(Cs4); plays(r.plan, {Cs4}); r.off(Cs4);
    r.on(C4); plays(r.plan, {B4 - 12});  // C sits between B and C#, and ties go down
    r.set(kFit, kMute);
    r.on(C4); plays(r.plan, {});  // an empty plan leaves the sounding root alone
    require(!r.off(C4));  // and its release still reaches stock
    r.on(C4); require(r.plan.count == 0); require(r.off(C4));
}

void capacity_and_bounds() {
    Rig r;
    const int whites[] = {C4, D4, E4, F4, G4, A4, B4, C5};
    for (int note : whites) { r.on(note); plays(r.plan, {note}); }
    r.on(D4 + 12); require(r.plan.count == 0);  // a ninth key is dropped
    require(!r.off(D4 + 12));
    require(!r.off(C4)); r.on(D4 + 12); plays(r.plan, {D4 + 12});
    require(r.on(128)); require(r.plan.count == 0); require(r.off(128));
    require(r.on(C4, 128)); require(r.plan.count == 0);
    require(!r.off(F3));  // never pressed: nothing of ours, stock decides
    r.set(kFit, kMute);
    r.on(Cs4); require(r.plan.count == 0);
    reset(r.state);
    require(!r.off(Cs4));
    for (const auto& root : r.state.roots) require(root.note == 0);
    for (u32 word : r.state.silent) require(word == 0);
    require(!r.pending());
}

void page_follows_the_settings() {
    Rig r;
    const u32 whites = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 11);
    const Display& major = r.draw();
    require(major.lit == (whites | whites << 12));
    require(std::string(major.text) == "C major");
    require(std::string(major.values[0]) == "C" && std::string(major.values[1]) == "maj");
    require(std::string(major.values[2]) == "near" && std::string(major.values[3]) == "off");
    require(std::string(major.values[4]) == "off" && std::string(major.values[5]) == "up");
    require(std::string(major.values[6]) == "norm" && std::string(major.values[7]) == "1 oct");

    r.set(kKey, 4); r.set(kScale, 1); r.set(kFit, kUp); r.set(kChord, kSeventh);
    const u32 e_minor = (1u << 1) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 11);
    const Display& minor = r.draw();
    require(minor.lit == (e_minor | e_minor << 12));
    require(std::string(minor.text) == "E minor");
    require(std::string(minor.values[0]) == "E" && std::string(minor.values[1]) == "min");
    require(std::string(minor.values[2]) == "up" && std::string(minor.values[3]) == "7th");

    r.set(kArp, kSixteenthTriplet); r.set(kDir, kUpDownRepeat); r.set(kLen, kLegato); r.set(kRange, 3);
    const Display& arp = r.draw();
    require(std::string(arp.values[4]) == "1/16T" && std::string(arp.values[5]) == "u/r/d");
    require(std::string(arp.values[6]) == "leg" && std::string(arp.values[7]) == "4 oct");

    r.set(kFit, kWhiteKeys);
    require(r.draw().lit == (whites | whites << 12));

    r.set(kFit, kNearest); r.set(kKey, 1); r.set(kScale, 7);
    require(std::string(r.draw().text) == "C# harmonic minor");
    r.set(kKey, 10); r.set(kScale, 9);
    require(std::string(r.draw().text) == "A# major pentatonic");
    require(std::string(r.shown.values[1]) == "pent");

    r.set(kKey, 0); r.set(kScale, 0);
    r.controls.lowest_key = 120;  // C9 up: keys past MIDI 127 cannot play, so they stay dark
    require(r.draw().lit == ((1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7)));

    r.controls.value[kKey] = 0x7fffffff;  // the top of the range is the last choice
    r.controls.value[kScale] = -1;
    require(std::string(r.draw().text) == "B major");
}

void every_scale_contains_its_root() {
    for (u32 scale = 0; scale < kScaleCount; ++scale) {
        const Scale& s = kScales[scale];
        require(s.steps[0] == 0 && s.count >= 5 && s.count <= 7);
        for (u32 i = 1; i < s.count; ++i) require(s.steps[i] > s.steps[i - 1] && s.steps[i] < 12);
        require(std::string(s.name).size() < 20);
        require(std::string(s.caption).size() < 8);
    }
    Rig r;
    r.set(kChord, kSeventh);
    for (u32 scale = 0; scale < kScaleCount; ++scale) {
        r.set(kScale, scale);
        r.on(C4);
        require(r.plan.count == 4 && (pitch_mask(r.plan) & 1u << (C4 % 32)));
        r.off(C4);
    }
}

// A bar of four quarter notes is 960 player steps.
void arp_rates_keep_time() {
    const u32 per_bar[kRateCount] = {0, 4, 6, 8, 12, 16, 24, 32, 48, 64};
    for (u32 rate = kQuarter; rate < kRateCount; ++rate) {
        Stage s;
        s.set(kArp, rate);
        s.down(C4);
        s.run(960);
        const auto played = notes(s);
        require(played.size() == per_bar[rate]);
        for (u32 i = 0; i < played.size(); ++i) require(played[i].start == i * 960 / per_bar[rate]);
        s.up(C4);
        require(quiet(s));
    }

    Stage s;
    s.set(kArp, kQuarter);
    s.down(C4);
    s.run(241);
    s.set(kArp, kSixteenth);  // the sounding note keeps its own beat
    s.run(400);
    const u32 starts[] = {0, 240, 480, 540, 600};
    const auto played = notes(s);
    require(played.size() == 5);
    for (u32 i = 0; i < 5; ++i) require(played[i].start == starts[i]);
}

void arp_lengths_set_the_gate() {
    const u32 gate[kLengthCount] = {15, 33, 60};  // of a sixteenth's 60 steps
    for (u32 length = kStaccato; length < kLengthCount; ++length) {
        Stage s;
        s.set(kArp, kSixteenth); s.set(kLen, length);
        s.down(C4);
        require(s.records.size() == 1);
        s.run(600);
        const auto played = notes(s);
        require(played.size() == 10);
        for (u32 i = 0; i + 1 < played.size(); ++i) {
            require(played[i].end - played[i].start == gate[length]);
            if (length == kLegato) require(played[i].end == played[i + 1].start);
        }
        require(s.records.size() == 1);
    }
}

void arp_directions() {
    const std::initializer_list<int> chord[] = {
        {C4, E4, G4, C4, E4, G4},
        {G4, E4, C4, G4, E4, C4},
        {C4, E4, G4, E4, C4, E4, G4},
        {C4, E4, G4, G4, E4, C4, C4, E4},
    };
    for (u32 direction = kUpward; direction <= kUpDownRepeat; ++direction) {
        Stage s;
        s.set(kArp, kSixteenth); s.set(kChord, kTriad); s.set(kDir, direction);
        s.down(C4);
        s.run(480);
        begins(s, chord[direction]);
    }

    // Keys pressed at the same moment all count toward the first note, in
    // whatever order the sequencer sends them.
    Stage up;
    up.set(kArp, kSixteenth);
    up.down(G4); up.down(C4); up.down(E4);
    up.run(360);
    begins(up, {C4, E4, G4, C4, E4, G4});

    Stage chords;
    chords.set(kArp, kSixteenth); chords.set(kChord, kTriad);
    chords.down(E4); chords.down(C4);
    chords.run(480);
    begins(chords, {C4, E4, G4, B4, C4, E4, G4, B4});

    // A key pressed after the first note joins from the next one.
    Stage late;
    late.set(kArp, kSixteenth);
    late.down(G4);
    late.run(1);
    late.down(C5);
    late.run(240);
    begins(late, {G4, C5, G4, C5});

    // Keys that join or leave mid-pattern neither repeat a note nor skip one.
    Stage lower;
    lower.set(kArp, kSixteenth);
    lower.down(G4);
    lower.run(1);
    lower.down(C4);
    lower.run(240);
    begins(lower, {G4, C4, G4, C4});

    Stage leaves;
    leaves.set(kArp, kSixteenth);
    leaves.down(C4); leaves.down(E4); leaves.down(G4);
    leaves.run(61);
    leaves.up(C4);
    leaves.run(240);
    begins(leaves, {C4, E4, G4, E4, G4});

    Stage turning;
    turning.set(kArp, kSixteenth); turning.set(kDir, kUpDown);
    turning.down(C4); turning.down(E4);
    turning.run(61);
    turning.down(G4);
    turning.run(300);
    begins(turning, {C4, E4, G4, E4, C4, E4, G4});

    // A held note an octave above another plays once per pass, not twice.
    for (u32 direction = kUpward; direction <= kAsPlayed; ++direction) {
        if (direction == kRandom) continue;
        Stage octaves;
        octaves.set(kArp, kSixteenth); octaves.set(kRange, 1); octaves.set(kDir, direction);
        octaves.down(C4); octaves.down(C5);
        octaves.run(60 * 6);
        const auto played = notes(octaves);
        for (u32 i = 0; i + 1 < played.size(); ++i)
            require(played[i].pitch != played[i + 1].pitch || direction == kUpDownRepeat);
        require(played[0].pitch == (direction == kDownward ? C5 + 12 : C4));
    }

    Stage order;
    order.set(kArp, kSixteenth); order.set(kDir, kAsPlayed);
    order.down(G4); order.down(C4); order.down(E4);
    order.run(360);
    begins(order, {G4, C4, E4, G4, C4, E4});

    Stage random;
    random.set(kArp, kThirtySecond); random.set(kChord, kTriad); random.set(kDir, kRandom);
    random.down(C4);
    random.run(1920);
    u32 seen[3] = {};
    for (const auto& note : notes(random)) {
        require(note.pitch == C4 || note.pitch == E4 || note.pitch == G4);
        ++seen[note.pitch == C4 ? 0 : note.pitch == E4 ? 1 : 2];
    }
    require(seen[0] > 8 && seen[1] > 8 && seen[2] > 8);

    Stage alone;  // one note has nowhere to turn
    alone.set(kArp, kSixteenth); alone.set(kDir, kUpDown);
    alone.down(C4);
    alone.run(240);
    begins(alone, {C4, C4, C4, C4});
}

void arp_range_adds_octaves() {
    Stage s;
    s.set(kArp, kSixteenth); s.set(kChord, kThird); s.set(kRange, 1);
    s.down(C4);
    s.run(300);
    begins(s, {C4, E4, C5, E4 + 12, C4});

    Stage down;
    down.set(kArp, kSixteenth); down.set(kChord, kThird); down.set(kRange, 2); down.set(kDir, kDownward);
    down.down(C4);
    down.run(420);
    begins(down, {E4 + 24, C5 + 12, E4 + 12, C5, E4, C4, E4 + 24});

    Stage top;  // octaves above MIDI 127 are left out
    top.set(kArp, kSixteenth); top.set(kRange, 3);
    top.down(115);
    top.run(240);
    begins(top, {115, 127, 115, 127});
}

void arp_rolls_chords_and_pools_keys() {
    Stage s;
    s.set(kArp, kSixteenth); s.set(kChord, kTriad);
    s.down(C4);
    require(s.records.size() == 1 && s.records[0].voices.size() == 1);
    s.run(180);
    s.down(E4);  // E G B joins C E G
    s.run(240);
    begins(s, {C4, E4, G4, B4, C4, E4, G4});
    require(s.records.size() == 1);
    s.up(C4); s.up(E4);
    require(quiet(s));

    Stage v;  // each note keeps its key's velocity; a shared pitch, the first key's
    v.set(kArp, kSixteenth);
    v.down(C4, 70); v.down(E4, 110); v.down(Cs4, 30);
    v.run(240);
    const auto played = notes(v);
    require(played.size() == 4);
    for (const auto& note : played) require(note.velocity == (note.pitch == C4 ? 70 : 110));

    Stage fit;  // the fit comes first
    fit.set(kArp, kSixteenth);
    fit.down(Cs4);
    fit.run(120);
    begins(fit, {C4, C4});
    fit.up(Cs4);
    fit.set(kFit, kMute);
    fit.down(Cs4);
    fit.run(120);
    require(notes(fit).size() == 2 && !fit.state.arp.running);
    require(fit.off(Cs4));
    require(quiet(fit));
}

void arp_runs_until_its_last_key_is_up() {
    Stage s;
    s.set(kArp, kSixteenth);
    s.down(C4); s.down(E4);
    s.run(80);
    s.up(C4);  // the key that started it
    require(s.records.size() == 1);
    s.run(240);
    auto played = notes(s);
    require(played.size() == 6);
    for (u32 i = 0; i < played.size(); ++i) {
        require(played[i].start == i * 60);
        if (i > 1) require(played[i].pitch == E4);
    }
    s.down(C4);  // joins again
    require(s.records.size() == 1);
    s.run(120);
    played = notes(s);
    require(played.size() == 8 && played[6].pitch == C4 && played[7].pitch == E4);
    s.up(E4);
    s.run(60);
    s.up(C4);
    played = notes(s);
    require(played.size() == 9 && played[8].pitch == C4);
    require(quiet(s));
    s.run(960);
    require(notes(s).size() == 9);

    Stage cut;  // the last key up silences the sounding note at once
    cut.set(kArp, kQuarter);
    cut.down(C4);
    cut.run(5);
    cut.up(C4);
    require(notes(cut).size() == 1 && notes(cut)[0].end == 5);
    require(quiet(cut));
    cut.down(E4);  // and the next key starts over
    cut.run(1);
    require(notes(cut).size() == 2 && notes(cut)[1].pitch == E4 && notes(cut)[1].start == 5);

    Stage left;  // a key let go between two notes is not played after all
    left.set(kArp, kQuarter);
    left.down(C4); left.down(E4);
    left.run(160);
    left.up(E4);
    left.run(120);
    require(notes(left).size() == 2 && notes(left)[1].pitch == C4 && notes(left)[1].start == 240);
}

void arp_off_goes_quiet_until_the_next_key() {
    Stage s;
    s.set(kArp, kQuarter);
    s.down(C4);
    s.run(10);
    s.set(kArp, kOff);
    s.run(1);
    require(s.records.empty() && notes(s)[0].end == 10);
    s.run(240);
    s.set(kArp, kQuarter);
    s.run(240);
    require(notes(s).size() == 1);  // the held key waits
    s.down(E4);
    s.run(241);
    begins(s, {C4, C4, E4});
    s.up(C4); s.up(E4);
    require(quiet(s));

    Stage plain;  // with the arp off, a new key sounds by itself
    plain.set(kArp, kEighth);
    plain.down(C4);
    plain.run(3);
    plain.set(kArp, kOff);
    plain.run(1);
    plain.down(E4);
    plain.run(100);
    const auto played = notes(plain);
    require(played.size() == 2 && played[1].pitch == E4 && played[1].end == kSounding);
    require(!plain.off(E4));
    plain.stop(E4);
    require(plain.off(C4));
    require(quiet(plain));
}

void plain_and_arp_keys_share_a_track() {
    Stage s;
    s.down(C4);  // held before the arp came on: it keeps sounding
    s.set(kArp, kSixteenth);
    s.down(E4); s.down(G4);
    require(s.records.size() == 2);
    s.run(240);
    begins(s, {C4, E4, G4, E4, G4});
    require(notes(s)[0].end == kSounding);
    s.up(C4);
    require(notes(s)[0].end == 240 && s.records.size() == 1);
    s.run(120);
    require(notes(s).size() == 7);
    s.up(E4); s.up(G4);
    require(quiet(s));
}

// A key pressed with the arp off sounds by itself, even on the arp's own key.
void a_plain_press_takes_the_arp_key() {
    Stage s;
    s.set(kArp, kSixteenth);
    s.down(C4); s.down(E4);
    s.run(80);
    s.up(C4);
    s.set(kArp, kOff);
    s.down(C4);
    require(s.records.size() == 1 && !s.state.arp.running);
    s.run(400);
    const auto played = notes(s);
    require(played.size() == 3 && played[1].end == 80);
    require(played[2].pitch == C4 && played[2].start == 80 && played[2].end == kSounding);
    s.up(C4);
    require(s.records.empty());
    s.up(E4);
    require(quiet(s));
}

void a_ninth_key_leaves_the_arp_alone() {
    Stage s;
    s.set(kArp, kSixteenth);
    const int keys[] = {C4, D4, E4, F4, G4, A4, B4, C5};
    for (int key : keys) s.down(key);
    s.down(D4 + 12);
    s.run(80);
    s.up(D4 + 12);
    require(s.records.size() == 1);

    s.up(C4);        // the arp's own key makes room,
    s.down(D4 + 12);
    s.down(C4);      // and comes back as the ninth
    s.run(80);
    s.up(C4);
    require(s.records.size() == 1 && s.state.arp.running);
    s.run(480);
    for (const auto& note : notes(s)) require(note.pitch != C4 || note.start < 80);
    for (int key : keys) s.up(key);
    require(s.records.size() == 1);
    s.up(D4 + 12);
    require(quiet(s));
}

// Random playing and knob turning leaves no note behind.
void nothing_hangs() {
    u32 seed = 1;
    const auto random = [&seed](u32 below) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % below;
    };
    Stage s;
    for (u32 i = 0; i < 50000; ++i) {
        const u32 note = 48 + random(24);
        switch (random(16)) {
        case 0: case 1: case 2: case 3: case 4: s.down(note, 1 + random(127)); break;
        case 5: case 6: case 7: case 8: s.up(note); break;
        case 9: case 10: {
            const u32 control = random(8);
            s.set(static_cast<Control>(control), random(kChoices[control]));
            break;
        }
        case 11: if (!random(50)) s.panic(); break;
        default: s.run(1 + random(80));
        }
        consistent(s);
    }
    for (u32 note = 0; note < 128; ++note) s.up(note);
    s.run(2);
    require(quiet(s));
}
}  // namespace

int main() {
    fits_cover_every_note();
    nearest_breaks_ties_downward();
    white_keys_play_the_scale();
    chords_stay_in_the_scale();
    released_pitches_sound_again_for_roots_still_holding_them();
    held_notes_keep_their_pitches();
    capacity_and_bounds();
    page_follows_the_settings();
    every_scale_contains_its_root();
    arp_rates_keep_time();
    arp_lengths_set_the_gate();
    arp_directions();
    arp_range_adds_octaves();
    arp_rolls_chords_and_pools_keys();
    arp_runs_until_its_last_key_is_up();
    arp_off_goes_quiet_until_the_next_key();
    plain_and_arp_keys_share_a_track();
    a_plain_press_takes_the_arp_key();
    a_ninth_key_leaves_the_arp_alone();
    nothing_hangs();
    std::puts("Every fit in every key and scale, white keys, chords, shared pitches, capacity, the arp and the page passed");
    return 0;
}
