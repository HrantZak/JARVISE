# Phase 6 — Human verification results

Fill this in as you go. One line per check, plus whatever you noticed.

Rules for filling it in, so the record stays worth something:

- A check you did not do is `BLOCKED — not performed`. Not blank, not "probably fine".
- A check that half-worked is `FAIL`, with what happened. Half-working is the
  interesting case; that is where the defects are.
- Record what you actually said and what actually came back, not a summary.

Machine: ___________________  Date: ___________  Build: `build\msvc-release\bin\JARVIS.exe`

Audible output available:  yes / **no**

With no speaker or headphones, checks 1 and 2 can only be done in half, and
check 8 stops being a test of the voice path. Each is marked below. Do not write
`PASS` on a line you could not hear.

---

## 1. A person speaks into the microphone

Status: `BLOCKED — not performed`

**Verifiable without audible output:**

Said:
Transcript shown:
Answer text shown on screen:
State moved LISTENING → TRANSCRIBING → THINKING?  yes / no
State returned to LISTENING, exactly once?  yes / no
Answer contains this machine's real figure (31.89 GB)?  yes / no

**Needs a speaker — leave blank without one:**

Spoken back:
Anything resembling JSON spoken aloud?  yes / no

If the top half is right and the bottom half is blank, the status is
`PARTIAL — input path verified, audible output not verifiable on this hardware`.

Notes:

---

## 2. Barge-in with a real voice

Status: `BLOCKED — not performed`

**Verifiable without audible output** (watch the screen while JARVIS is in
SPEAKING and start talking):

State left SPEAKING when you spoke?  yes / no
Agent page showed no running task afterwards?  yes / no
No error banner appeared from the barge-in itself?  yes / no
A cough or keyboard clatter did **not** interrupt?  yes / no

**Needs a speaker:**

Playback audibly stopped within a sentence?  yes / no

Notes:

---

## 3. Approving `open_application`

Status: `BLOCKED — not performed`

Allow → calculator opened, once?  yes / no
Deny → nothing opened, and JARVIS said so?  yes / no
**Asking again raised a second dialog?**  yes / no

That last line is the security-relevant one. If the second request runs without
asking, the approval is being replayed and that is a defect, not a convenience.

Notes:

---

## 4. The confirmation dialog itself

Status: `BLOCKED — not performed`

Names the tool and the exact arguments?  yes / no
Default action is the safe one?  yes / no
Escape cancels rather than approves?  yes / no
Times out, and the timeout denies?  yes / no

Notes:

---

## 5. Stop pressed mid-task, by hand

Status: `BLOCKED — not performed`

Task ended, step list cleared, state back to IDLE?  yes / no
**No answer arrived afterwards?**  yes / no
No error banner from the Stop itself?  yes / no

A late answer seconds after Stop means the generation guard is not holding.

Notes:

---

## 6. The agent page during a multi-step plan

Status: `BLOCKED — not performed`

Each step showed its tool, status and attempt count?  yes / no
Progress never ran ahead of finished work?  yes / no
Confirming steps looked different from running ones?  yes / no

Notes:

---

## 7. Both interface languages

Status: `BLOCKED — not performed`

Every visible string changed on switching?  yes / no
Nothing fell back to untranslated English?  yes / no
No label clipped by the longer language?  yes / no
Tool names, state keys and model names stayed untranslated?  yes / no

Notes:

---

## 8. Sustained load and thermals

Status: `BLOCKED — not performed`

Exchanges were:  spoken / **typed** (circle one)

Typed exchanges still load both models and still stress VRAM and the GPU, but
they skip Whisper and Piper. If you typed them, the result is about the model
under load, not about the voice path under load — say so rather than letting the
line read as more than it is.

Exchanges completed: ___ / 20
Fallback to CPU at any point?  yes / no
Load failure on the last exchange?  yes / no
Peak VRAM: ______ / 8.00 GB
Peak GPU temperature: ______ °C

Reference figures under this load: 7.40 GB and 68 °C.

Notes:

---

## Summary

| # | Check | Status |
|---|---|---|
| 1 | Speaks into the microphone | |
| 2 | Barge-in with a real voice | |
| 3 | Approving `open_application` | |
| 4 | The confirmation dialog | |
| 5 | Stop pressed by hand | |
| 6 | Agent page during a plan | |
| 7 | Both languages | |
| 8 | Sustained load | |

Copy the finished statuses into the table at the top of
[phase6-human-verification.md](phase6-human-verification.md).
