# Testing Scales on an OP-XY

Checks to run on a device with a firmware that includes Scales, from top to bottom, in one
project: each section leaves the project ready for the next. Host tests
(`python ../open-xy/sdk.py test --machine .`) don't replace these: they can't hear the notes or
see the screen.

Report a failure with the section, the line and what you saw. A photo of the screen helps.

Keep this file in step with the player: a change that adds, removes or alters something a person
would check on the device updates its lines here. Note the SDK commit and firmware each full pass used.

## Setup

- Build a firmware with the player ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md#3-add-your-machine))
  and install it.
- Create a new project, **plain**, without Scales: give track 1 a stock sound and save it. Later
  steps load it as a project that never had the player.
- Create another new project, **qa**, for the pass: don't use a project you care about.
- Track 1: a stock synth with a clear pitch, such as a simple saw. Open its player and choose Scales.
- Leave the sequencer stopped unless a step says otherwise.

## Firmware basics

- [ ] The OP-XY boots, and stock engines, effects, the sequencer and project save/load work
- [ ] Scales is in the player list beside Arpeggio, Hold and Maestro, under its name
- [ ] Choosing it opens its page: the stock header with key, scale, fit and chord, two octaves of
      keys with dots, the line "C major" and a small keyboard along the bottom

## Its page and controls

- [ ] Key steps C, C#, D ... B one per detent, both directions, and stops at the ends; the dots move
      to the new key's notes and the line names it
- [ ] Scale steps through its twelve choices one per detent; the dots and the line follow
- [ ] Fit and Chord step through their choices one per detent; the header value reads near, down, up,
      mute, white and off, 3rd, triad, 7th
- [ ] Fast turns: the values keep up, no flicker or slowdown
- [ ] Shift + an octave key: the keyboard shifts and the dots follow the scale on the new octave
- [ ] The page while playing and while stopped, and after leaving it and coming back: nothing stale,
      missing or drawn twice
- [ ] Play a few keys: the bottom keyboard lights the notes that sound, including moved ones

## Fits

Set Key to C, Scale to maj and Chord to off.

- [ ] near: every white key plays itself; each black key plays the white key below it
- [ ] up: each black key plays the white key above it
- [ ] down: each black key plays the white key below it
- [ ] mute: black keys are silent; releasing them cuts nothing
- [ ] white: all keys play themselves and the black keys are silent
- [ ] Key E, Scale min, fit white: the lower C plays E, and the white keys upward play F#, G, A, B,
      C, D, E; every white key has a dot and no black key does
- [ ] Key G, Scale maj, fit white: the lower C plays G below it, D plays A, and so on up
- [ ] Scale pent, fit white, Key C: the white keys play C D E G A, then C an octave up
- [ ] Fit near, Scale min, Key E: F and the other out-of-scale keys move, the rest play as written

## Chords

Key C, Scale maj, fit near.

- [ ] 3rd: C plays C and E; B plays B and D
- [ ] triad: C plays C E G; D plays D F A; B plays B D F
- [ ] 7th: G plays G B D F
- [ ] A black key with a chord on: the chord is built on the note it moved to
- [ ] The highest keys with 7th on: tones that would pass the top of the range are left out, the rest sound

## Held notes

Key C, Scale maj, fit near, Chord off.

- [ ] Hold C and C# together: one C sounds. Release C: C still sounds for the held C#. Release C#: silence
- [ ] The same the other way round: hold C#, then C, release C#, then C
- [ ] Hold a note and turn Key: the note keeps its pitch; the next press follows the new key
- [ ] Hold a note and turn Fit to mute, press the same key again, release it: the note stops
- [ ] Hold eight white keys: all sound. Press a ninth: nothing. Release one and press it again: it sounds
- [ ] Press stop with keys held: everything stops, and the next presses play normally

## Sequencer

- [ ] Record a bar with white and black keys on track 1 with Key C, Scale maj, fit near: playback
      moves the black keys as live playing did
- [ ] While it plays, turn Key to D: the pattern moves to D major; turn Scale to min: it moves again
- [ ] Record a note below or above the keyboard's two octaves (shift the octave first): it follows the same map
- [ ] Give the track a second pattern with different values: each pattern keeps its own, and switching
      between them while playing and while stopped follows at once
- [ ] Copy a pattern: the copy holds the same values, and changing one leaves the other alone
- [ ] Make scenes 1 and 2 with the two patterns, and a song of scene 1 then scene 2, looping: each scene
      plays its own values from its first bar, on every pass

Then play scene 1.

## Other players and tracks

- [ ] Switch the track's player to Arpeggio and back: Scales returns with its values
- [ ] Scales on track 2 with different values at the same time: each track keeps its own
- [ ] Mute track 1 (hold aux and press its key) while notes sound: silence, no stuck notes; unmute: it plays

## Saving and loading

- [ ] Save **qa**, power off and on, load it: the player is there with every value as you left it, on
      each pattern
- [ ] Change a value, wait a few seconds and power off without saving: the OP-XY starts normally, and
      **qa** loads as last saved
- [ ] Load **plain**: track 1 has its own sound and player, nothing of **qa** shows. Load **qa** again: all back
- [ ] While playing, load **plain** and then **qa** again: nothing freezes, no stray values
- [ ] Create a new project: put Scales on a track; it starts at C, maj, near, off
- [ ] On a firmware without Scales, open **qa**: it loads, track 1 plays with Arpeggio, and the rest of
      the project plays

## In the builder

Once the player's catalog entry is merged and released (the beta site from `staging`, open-xy.com from
`main`; see [submitting a machine](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md#submit-a-machine)):

- [ ] The machine list shows Scales as Community, with its name, maintainer and description
- [ ] Filtering by its kind (players) lists it, and filtering by the other kinds doesn't
- [ ] Searching its name, and each of its keywords, finds it
- [ ] Choosing it builds a firmware that includes it, and that firmware passes
      [Firmware basics](#firmware-basics)

## Known limits

Not failures, but worth confirming they haven't changed.

- A note a chord adds to a held note is not re-sounded when it is also released by another key's
  chord; only pitches shared between two held keys are.
- Re-sounding a shared pitch restarts its envelope.
- Players have no CPU record: the profiler does not cover note transforms.
