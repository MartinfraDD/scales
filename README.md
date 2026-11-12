# Scales

Scales is a note player for the OP-XY that keeps a track in one key. Every note
the track receives, played live or from the sequencer, passes through it before
the instrument. Notes in the chosen scale play as they are. Notes outside it are
moved, silenced or remapped, depending on the fit, and a chord setting can stack
scale thirds on each note. An arp can then play the held notes one at a time.

## Controls

Press any encoder to switch between the two pages.

| Page | Encoder | Control | Choices |
| --- | --- | --- | --- |
| 1 | 1 | key | C to B |
| 1 | 2 | scale | major (maj), minor (min), dorian (dor), phrygian (phr), lydian (lyd), mixolydian (mix), locrian (loc), harmonic minor (hmin), melodic minor (mmin), major pentatonic (pent), minor pentatonic (mpent), blues |
| 1 | 3 | fit | near, down, up, mute, white |
| 1 | 4 | chord | off, 3rd, triad, 7th |
| 2 | 1 | arp | off, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64 |
| 2 | 2 | dir | up, down, up/dn, u/r/d, rand, order |
| 2 | 3 | len | stac, norm, leg |
| 2 | 4 | range | 1 to 4 oct |

A new track starts in C major with fit near, no chord and the arp off.

**Fit** decides what happens to a note outside the scale:

- **near** moves it to the closest scale note, down when two are equally close.
- **down** and **up** move it to the next scale note in that direction.
- **mute** keeps it silent.
- **white** plays the scale on the white keys and silences the black keys, so
  every scale has the fingering of C major. The keyboard's lower C plays the root.

**Chord** adds scale notes above each note: the third, the third and fifth, or
up to the seventh. They are built from the scale, so they stay in key.

**Arp** plays the held notes one at a time, at the same rates as the stock
arpeggio and in time with the project tempo. T marks triplets. Fit and chord
apply first, so one key with a chord plays that chord's notes in turn.

- **dir** is the order: up, down, up and down (up/dn), up and down with the top
  and bottom notes twice (u/r/d), random (rand), or the order you pressed the
  keys (order).
- **len** is how much of each step a note lasts: a quarter (stac), about half
  (norm) or all of it (leg).
- **range** repeats the notes up to three octaves higher.

The screen shows two octaves of keys with a dot on each key that plays
unmoved, the key and scale by name, and the notes sounding along the bottom.
The header shows both pages, with the one the encoders turn in full size.

## Playing

- Turning a control leaves held notes as they are. It applies from the next press.
- When two held keys land on the same note, releasing one restarts the note for the other.
- Up to eight keys play at once.
- The arp plays the keys you hold and stops when you let go of the last one.
- Turning the arp on leaves notes already held sounding. Turning it off silences
  the keys it was playing until you press them again.
- The sequencer stores the keys you played, so changing Key or Scale moves a recorded pattern.

## Develop

Keep the [Open-XY SDK](https://github.com/etcetc000/open-xy) beside this
repository as `../open-xy`. Run the tests, or build a firmware from your own
copy of the OP-XY's OS:

```sh
python ../open-xy/sdk.py test --machine .
python ../open-xy/sdk.py build --machine . --stock-os <stock>/os.ldr --out <build>
```

The player is [player.cpp](player.cpp) and its tests are in
[tests/scales.cpp](tests/scales.cpp). `python artwork.py` regenerates
[panel.svg.fragment](panel.svg.fragment). [QA.md](QA.md) is the checklist for
testing on the device.
