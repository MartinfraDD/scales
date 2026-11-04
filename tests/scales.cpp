#include "../player.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

#define require(x) \
    do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

using namespace opxy;
using namespace opxy::player;
using namespace scales;

namespace {
enum Control { kKey, kScale, kFit, kChord };
enum Note { F3 = 53, C4 = 60, Cs4 = 61, D4 = 62, E4 = 64, F4 = 65, G4 = 67, A4 = 69, B4 = 71, C5 = 72 };

const u32 kChoices[4] = {12, kScaleCount, kFitCount, kChordCount};

// The value stock stores for an enum choice: the centre of its bucket.
i32 centre(u32 index, u32 count) {
    return static_cast<i32>(((2ull * index + 1) << 30) / count);
}

struct Rig {
    State state = {};
    Controls controls = {{0, 0, 0, 0}, F3};
    Plan plan = {};
    Update change = {};
    Display shown = {};

    void set(Control control, u32 index) { controls.value[control] = centre(index, kChoices[control]); }
    bool on(u32 note, u32 velocity = 90) { return press(state, controls, note, velocity, plan); }
    bool off(u32 note) { return release(state, note); }
    bool pending() { return update(state, controls, change); }
    const Display& draw() { display(state, controls, shown); return shown; }
};

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

    r.set(kKey, 4); r.set(kScale, 1); r.set(kFit, kUp); r.set(kChord, kSeventh);
    const u32 e_minor = (1u << 1) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 9) | (1u << 11);
    const Display& minor = r.draw();
    require(minor.lit == (e_minor | e_minor << 12));
    require(std::string(minor.text) == "E minor");
    require(std::string(minor.values[0]) == "E" && std::string(minor.values[1]) == "min");
    require(std::string(minor.values[2]) == "up" && std::string(minor.values[3]) == "7th");

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
    std::puts("Every fit in every key and scale, white keys, chords, shared pitches, capacity and the page passed");
    return 0;
}
