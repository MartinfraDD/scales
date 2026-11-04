# Scales note player

Scales keeps a track in one key. Every note the track receives, played live or
from the sequencer, passes through it before the instrument. A note in the
chosen scale plays as it is. A note outside it moves to a scale note, stays
silent or plays from the white keys, depending on the fit. A chord setting
stacks scale thirds on each note.

## Encoders and screen

| Encoder | Caption | Control | Choices |
| --- | --- | --- | --- |
| 1 | key | Key | C to B |
| 2 | scale | Scale | maj, min, dor, phr, lyd, mix, loc, hmin, mmin, pent, mpent, blues |
| 3 | fit | Fit | near, down, up, mute, white |
| 4 | chord | Chord | off, 3rd, triad, 7th |

Every control moves one choice per encoder detent. Defaults are C, maj, near
and off, so a new track plays C major with nothing moved and no chord.

The scales are major, natural minor, the five other modes (dorian, phrygian,
lydian, mixolydian, locrian), harmonic minor, melodic minor, major and minor
pentatonic, and the six-note blues scale.

The fit decides what a note outside the scale does:

- **near** moves it to the closest scale note. When the notes above and below
  are equally close, it moves down.
- **down** and **up** move it to the next scale note in that direction. At the
  very ends of the MIDI range, with no scale note left in that direction, the
  note is silent.
- **mute** keeps it silent.
- **white** ignores the scale's own layout: the seven white keys play the
  scale in order and the black keys are silent, so every scale has the
  fingering of C major. The keyboard's lower C plays the key's root nearest
  it (E4 for E, G3 for G), and each white key above or below continues the
  scale from there. Five- and six-note scales wrap sooner: in a pentatonic,
  the sixth white key is the root an octave up.

The chord adds scale notes above whatever a note has become: **3rd** adds
the third, **triad** the third and fifth, **7th** the seventh as well. They
are built from scale degrees, so they stay in key: B in C major gives a
diminished triad. Tones above MIDI 127 are left out.

The screen shows two octaves of keys with a dot on every key that plays
unmoved (with the white fit, on every white key), the key and scale by name,
and along the bottom the notes actually sounding.

## Playing

- A held note keeps its pitches when a control is turned. The new setting
  applies from the next press.
- Two keys can land on the same pitch, such as C and C sharp in C major.
  Releasing one would stop that pitch for the other, so Scales sounds it
  again for the key still held. That restarts the note.
- Up to eight keys can sound together. A ninth is dropped until one is released.
- Octave shifts move the keyboard and the dots follow. With the white fit, the
  lower C moves with the keyboard.
- The sequencer stores the keys you played, so turning Key or Scale moves a
  recorded pattern. Sequenced notes outside the keyboard follow the same map.
- Stop and all-notes-off clear held keys.

## Build and tests

Requires SDK 1.8. Run the host tests with

```sh
python ../open-xy/sdk.py test --machine .
```

[tests/scales.cpp](tests/scales.cpp) checks every fit in every key and
scale across the MIDI range, the white-keys map, chords, shared pitches, held
notes, capacity and the page. The player is [player.cpp](player.cpp).
Regenerate [panel.svg.fragment](panel.svg.fragment) with `python artwork.py`.

Build a firmware with
`python ../open-xy/sdk.py build --machine . --stock-os <stock>/os.ldr --out <build>`
and choose **scales** as the track's player. Keep the resulting `layout.json`
for later builds, so projects keep the player; stock firmware loads an unknown
saved player as Arpeggio. The SDK's
[build guide](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md)
covers the toolchain and installing. Keep firmware and stock content outside
every checkout.

## Develop

Keep the [SDK](https://github.com/etcetc000/open-xy) beside this repository as
`../open-xy`, on `staging`. Its
[machine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md#note-players)
describes note players, and the
[Chords](https://github.com/etcetc000/open-xy-core-machines/tree/main/machines/chords)
player is the worked example this one follows. The
[UX conventions](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md)
apply to its controls and page.

The generated workflow checks the source and host tests on Linux and compiles
the native module on Windows; it never uses a device. Its `OPEN_XY_REF` starts
at `staging`; pin a compatible SDK commit for a release.

## Hardware and publication

Check the player on the device with [QA.md](QA.md): one pass, top to bottom,
in one project. Host tests cannot hear the notes or see the screen.

Note players carry no CPU record in the catalog, since the profiler does not
cover note transforms. Publication follows
[catalog submission](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine).

Covered by [LICENSE](LICENSE).
