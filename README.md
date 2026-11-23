# UNDERTONE 1.0 (Digitakt MKI, OS 1.54)

A subharmonic machine for the Digitakt MKI, inspired by how the Moog Subharmonicon builds its
sound and by the Labyrinth's wavefolder. Two oscillators, each with two subharmonics (the
oscillator's pitch divided by 1–16), saw to square, the second oscillator an interval away, a
wavefolder and a slow analog drift. Pick it with **FUNC + SRC**. The Digitakt's filter, amp, LFO
and FX follow, as on a sample.

## Page 1

| Knob | |
|---|---|
| TUNE | pitch |
| WAVE | saw to square, for all six tones. Its picture is the wave |
| SUB1, SUB2 | oscillator 1's two subharmonics: its pitch divided by 1–16 |
| INTV | oscillator 2's interval from oscillator 1, −24 to +24 semitones |
| SUB3, SUB4 | oscillator 2's two subharmonics, divided by 1–16 |
| LEV | level |

## Page 2 (press SRC again)

| Knob | |
|---|---|
| FINE | oscillator 2's fine detune |
| OSCL | the level of the two oscillators |
| SUBL | the level of the four subharmonics |
| FOLD | a wavefolder, from clean to folded and bright |
| QNT | how INTV is tuned: **FREE** (with FINE), **ET** (equal temperament) or **JI** (just intonation: pure ratios, beatless chords) |
| DRFT | a slow analog drift of the six pitches |
| VIEW | **LADR**: the six tones on a pitch ladder. **WAVE**: the waveform. The picture fills the last two knob spaces |

## Playing it

- The subharmonics are what make it: a SUB of 2 is an octave down, 3 an octave and a fifth down,
  4 two octaves down. Odd and higher divisions give the Subharmonicon's strange undertone chords.
- Set QNT to **JI** for the purest, most locked-together intervals, or **FREE** and FINE for a
  slow beating.
- A little DRFT makes it sound like an old analog voice.

## The sequencers

UNDERTONE's sequencers (the Subharmonicon-style polyrhythms and the Labyrinth-style corrupt mode)
now live in **FREQSEQ**, in [flexmods](https://github.com/ceredmeat/FLEXMODS), as its POLYRHYTHM
and CORRUPT modes. They can run on any track.

## Install

Load `undertone-1.0.elemod` in Elekloader with **core 4.4** and **machine-pages** (1.11), build your
OS and flash it. No `.syx` is shared here: it would contain Elektron's firmware.

## Tested on a unit

Tested on a Digitakt MKI, OS 1.54.

## Notes

Needs **core 4.4** and **machine-pages**. Machine id 35. Page 2 uses sound slots 46–52, like every
machine-pages machine with a second page, so on an UNDERTONE track don't use FX Locks or Deja Vu's
RND destinations.

Licence: GPL-2.0-or-later. By Cere, made with Claude. Not affiliated with Moog Music or Bastl
Instruments.

## Changes

| Version | What changed |
|---|---|
| 1.0 | The screen code is built smaller, which leaves more RAM for your other mods. The sound is unchanged. |
