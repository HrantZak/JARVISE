# Phase 6 — Human verification

The checks no automated test performs, and why each one needs a person.

Nothing in this document has been performed by the automated suite. Where a
check is marked **BLOCKED**, it means exactly that: it has not been done, and no
test result should be read as covering it.

## Status

| # | Check | Status |
|---|---|---|
| 1 | A person speaks into the microphone | **BLOCKED — not performed** |
| 2 | Barge-in with a real voice | **BLOCKED — not performed** |
| 3 | Approving `open_application` | **BLOCKED — not performed** |
| 4 | The confirmation dialog under a real request | **BLOCKED — not performed** |
| 5 | Stop pressed mid-task, by hand | **BLOCKED — not performed** |
| 6 | The agent page during a multi-step plan | **BLOCKED — not performed** |
| 7 | Both interface languages, read by a reader | **BLOCKED — not performed** |
| 8 | Sustained load and thermals | **BLOCKED — not performed** |

**BLOCKED** here does not mean broken. It means unverified by a person. The
automated equivalents are green — see [phase6-testing.md](phase6-testing.md) —
but an automated equivalent is not the check.

### Hardware available on the reference machine

Recorded because it decides which of these can be attempted at all.

| | |
|---|---|
| Microphone | present and working — `Microphone (Realtek(R) Audio)`, verified by `tst_microphone_capture` |
| Audible output | **none** — no speakers and no headphones connected |

Windows reports three active render endpoints (`PHL 241V8`, `Realtek Digital
Output`, `Speakers`) and Qt opens one, which is why `tst_audio_player` runs
rather than skipping. That is the audio stack working; it is not sound reaching
a person. Nothing is plugged in.

Consequence for this document:

- Checks **3, 4, 5, 6, 7** need only a keyboard and the screen. They can be done
  today.
- Checks **1 and 2** split. The half that goes *into* the machine — the
  microphone, the transcript, the state machine, the agent's answer on screen —
  is verifiable. The half that comes *out of* it is not: nobody can confirm what
  was spoken aloud, or that playback stopped mid-sentence, without hearing it.
  Record those as `PARTIAL` with the audible half left `BLOCKED`, never as
  `PASS`.
- Check **8** can be run with typed exchanges. VRAM and thermals are real either
  way, but it would not then be the *voice* path under load, and must say so.

Plugging in any headphones lifts all of this. Until then the split above is the
honest state.

## Automated hardware evidence (not a substitute for check 1)

`tst_microphone_capture` opens the real capture device and records what
happened. It is listed here because it changes what check 1 has left to prove,
and for no other reason: **it is not a human check and does not close one.**

Measured on the reference machine:

```
input devices            : 2
  - Microphone (Realtek(R) Audio)  [default]
  - Microphone (Steam Streaming Microphone)
default input            : Microphone (Realtek(R) Audio)
capture selects          : Microphone (Realtek(R) Audio)
format                   : 16000 Hz, 1 ch, s16
blocks delivered         : 203
samples captured         : 47969 (expected about 48000)
RMS level                : 0.001795
peak level               : 0.010254
signal                   : non-zero samples present
```

Written to `build/<config>/tests/microphone-capture-report.txt` on every run.

What that establishes: a physical input device exists, is the system default, is
selected by `AudioCapture`, opens, and delivers PCM at the rate it claims — 203
blocks and 47,969 samples where 48,000 were expected in three seconds — already
converted to the 16 kHz mono float the rest of the pipeline is written against,
with every sample in range.

What it does **not** establish, and what check 1 still exists for: that anything
*intelligible* was said. An RMS of 0.0018 is a quiet room, not a voice. The test
deliberately does not assert a signal level, because a silent room is not a
defect and a test that demanded sound would fail on a correct machine. A device
delivering ambient noise and a device delivering a person's question are
identical to it.

So: the hardware path is verified as far as software can verify it, and the
human path is untouched.

## Why the rest cannot be tested automatically

`tst_voice_agent_live` runs the whole spoken path with real engines, but Piper
speaks the question instead of a person. That substitution is honest and it is
stated everywhere it applies, and it leaves four things uncovered:

- the microphone's gain and placement in a real room
- room acoustics, reverberation and background noise
- a human voice: pace, accent, hesitation, an unfinished sentence
- the VAD's thresholds against any of the above

A test process cannot make a sound in a room. That is the entire reason this
document exists.

## The checks

### 1. A person speaks into the microphone

1. Launch `build\msvc-release\bin\JARVIS.exe`.
2. Turn voice on. Wait for the state strip to read `LISTENING`.
3. Say, in Russian: *«Сколько у меня оперативной памяти?»*

Expected:
- the transcript appears and is recognisably what you said
- the state moves `LISTENING → TRANSCRIBING → THINKING`
- **nothing that looks like JSON is spoken aloud** — no braces, no `memory_info`
- the spoken answer contains this machine's real figure (31.89 GB here)
- the state returns to `LISTENING` after the answer, once

Record: what you said, what the transcript said, what was spoken back.

The device itself is already known to work — see the evidence above — so if this
check fails, the fault is in recognition, in the VAD thresholds, or in the room,
not in whether PCM arrives.

### 2. Barge-in with a real voice

While JARVIS is speaking a long answer, start talking.

Expected: playback stops within a sentence, the state returns to `LISTENING`,
and the agent's task is stopped — not merely muted. Check the agent page shows
no running task afterwards.

Worth trying twice: once by speaking clearly, and once with a cough or a
keyboard clatter, which must **not** interrupt.

### 3. Approving `open_application`

No automated test presses Allow, because approving it launches a real window.

1. Ask JARVIS to open the calculator.
2. The confirmation dialog appears, naming the tool and the argument.
3. Press Allow.

Expected: the calculator opens, once. Then repeat and press Deny: nothing opens,
and the agent says so rather than going silent.

Then the one that matters: ask again immediately. **A second dialog must
appear.** An approval is spent by the call it was given for; if the second
request runs without asking, the grant is being replayed and that is a security
defect, not a convenience.

### 4. The confirmation dialog under a real request

Look at the dialog itself, not the behaviour:

- does it name the tool and the exact arguments?
- is the default action the safe one?
- does Escape cancel rather than approve?
- does it time out, and does the timeout deny?

### 5. Stop pressed mid-task, by hand

Start a multi-step plan (turn the planner on in settings), then press Stop while
a step is running.

Expected: the task ends, the step list clears, the state returns to `IDLE`, and
**no answer arrives afterwards**. A late answer appearing seconds after Stop
would mean the generation guard is not holding — that is
`tst_agent_loop::aLateResultCannotReviveAStoppedTask` failing in the real
application.

### 6. The agent page during a multi-step plan

Watch the page while a three-step plan runs. Each step should show its tool, its
status and its attempt count, and progress should never run ahead of finished
work. Confirming steps should be visually distinct from running ones.

### 7. Both interface languages

Switch to English and back to Russian, with the application running.

Expected: every visible string changes, nothing falls back to the untranslated
source, and no label is clipped by the longer of the two. Technical names —
tool names, state keys, model names — must **not** be translated.

A reader of each language should read the interface once. 484 translated strings
being present is not the same as their being right.

### 8. Sustained load and thermals

Whisper and Qwen3 are resident together on an 8 GB card. Run twenty spoken
exchanges in a row and watch VRAM and GPU temperature in the HUD.

Expected: no fallback to CPU, no load failure on the twentieth exchange, and
temperatures within the card's normal range. The reference machine's RTX 3070
sits at 68 °C under this load with 7.40 GB of 8.00 GB in use — which is close
enough to the ceiling that this check is not a formality.

## Reporting

For each check, record: what was done, what happened, and the machine. A check
that was skipped is recorded as skipped, not as passed. If a check fails, the
failure class vocabulary in [phase6-testing.md](phase6-testing.md) applies here
too — naming `SECURITY_FAILURE` versus `MODEL_BUDGET_EXHAUSTED` is worth more
than a description of the symptom.
