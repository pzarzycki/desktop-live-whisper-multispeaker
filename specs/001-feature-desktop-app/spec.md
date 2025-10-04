# Feature Specification: Desktop live transcription with speaker diarization and color‑coded UI

**Feature Branch**: `001-feature-desktop-app`  
**Created**: 2025-10-03  
**Status**: Draft  
**Input**: User description: "This is a Desktop application that allows user to turn on and off LISTENING to the microphone and the audio stream is automatically transcribed to the text with Whisper model. Additionally speaker identification is performed (not exactly who is who, but how many speakers, so each speaker text is separated properly). The transcribed text, colored by the speaker ident, is displayed in the application (scrolling when necessary)"

## Execution Flow (main)

```text
1. Parse user description from Input
   → If empty: ERROR "No feature description provided"
2. Extract key concepts from description
   → Identify: actors, actions, data, constraints
3. For each unclear aspect:
   → Mark with [NEEDS CLARIFICATION: specific question]
4. Fill User Scenarios & Testing section
   → If no clear user flow: ERROR "Cannot determine user scenarios"
5. Generate Functional Requirements
   → Each requirement must be testable
   → Mark ambiguous requirements
6. Identify Key Entities (if data involved)
7. Run Review Checklist
   → If any [NEEDS CLARIFICATION]: WARN "Spec has uncertainties"
   → If implementation details found: ERROR "Remove tech details"
8. Return: SUCCESS (spec ready for planning)
```

---

## ⚡ Quick Guidelines

- ✅ Focus on WHAT users need and WHY
- ❌ Avoid HOW to implement (no tech stack, APIs, code structure)
- 👥 Written for business stakeholders, not developers

### Section Requirements

- **Mandatory sections**: Must be completed for every feature
- **Optional sections**: Include only when relevant to the feature
- When a section doesn't apply, remove it entirely (don't leave as "N/A")

### For AI Generation

When creating this spec from a user prompt:

1. **Mark all ambiguities**: Use [NEEDS CLARIFICATION: specific question] for any assumption you'd need to make
2. **Don't guess**: If the prompt doesn't specify something (e.g., "login system" without auth method), mark it
3. **Think like a tester**: Every vague requirement should fail the "testable and unambiguous" checklist item
4. **Common underspecified areas**:
   - User types and permissions
   - Data retention/deletion policies  
   - Performance targets and scale
   - Error handling behaviors
   - Integration requirements
   - Security/compliance needs

---

## Clarifications

### Session 2025-10-03

- Q: How should we handle speaker diarization for v1 on Windows? → A: Real-time local embeddings +
   incremental clustering (Option A)

- Q: Whisper model scope for v1? → A: Whisper base/small only (balanced accuracy/latency) (Option B)

- Q: Audio capture mode on Windows? → A: WASAPI Shared, default input, 16 kHz mono (Option A)

- Q: UI tech within Qt6 for v1? → A: Qt Quick/QML (Option B)

- Q: Persistence/export of transcripts in v1? → A: No persistence in v1 (session-only) (Option A)

---
 
 

## User Scenarios & Testing *(mandatory)*

### Primary User Story

As a user, I can press a single control to start “Listening” so that my microphone is captured and
spoken words are transcribed into text in near real-time. If multiple people are speaking, the text
is grouped by anonymous speaker labels (e.g., Speaker 1, Speaker 2) and each speaker’s text is
visually distinguished with a consistent color. I can press the same control to stop Listening.
The transcript view stays readable and automatically scrolls as new lines arrive.

### Acceptance Scenarios

1. Given the app is idle (not Listening), when the user clicks “Listen”, then the app requests mic
    permission if needed, begins capturing audio, and the transcript area starts showing text updates
    within one second.
2. Given the app is Listening, when the user clicks “Stop”, then audio capture and transcription
    stop, the UI indicates Listening is off, and no new text appears.
3. Given multiple concurrent speakers, when the app is Listening, then the transcript shows
    separate speaker-labeled segments (Speaker N), each with a consistent color across the session.
4. Given a long speaking session, when the transcript exceeds the visible area, then the transcript
    view auto-scrolls to keep the latest text visible, while allowing manual scrollback.
5. Given the mic is unavailable or permission is denied, when the user clicks “Listen”, then the app
    shows a clear error message and remains not Listening.

### Edge Cases

- Very noisy or overlapping speech: diarization accuracy is limited; text still appears but speaker
   grouping might degrade gracefully.
- Rapid toggle: user starts and stops Listening quickly (debounce so the state is consistent).
- No microphone devices present; microphone busy/locked by another app.
- Very long sessions (1+ hours): ensure the transcript remains responsive; transcript is in-memory
   only and not persisted after exit; pagination behavior TBD. [NEEDS CLARIFICATION]
- Language/locale of speech and UI: which languages are in scope at launch? [NEEDS CLARIFICATION]
- Processing is local-only in v1.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST provide a clear control to start and stop Listening (microphone capture).
- **FR-002**: When Listening, the system MUST transcribe spoken audio into text in near real-time
   with visible updates within 1 second for typical speech.
- **FR-003**: The system MUST separate the transcript into segments per anonymous speaker label
   (e.g., Speaker 1, Speaker 2) when multiple speakers are detected.
- **FR-004**: The UI MUST display each speaker’s text with a consistent, distinct color during a
   session and ensure readability (sufficient contrast).
- **FR-005**: The transcript view MUST automatically scroll to keep the most recent text visible,
   while allowing the user to scroll up and read earlier text without the view force-jumping.
- **FR-006**: The system MUST handle microphone permission prompts and error states (no device,
   device busy, permission denied) with clear user messaging and remain stable.
- **FR-007**: The system MUST resume cleanly after stopping, allowing the user to start Listening
   again without restarting the app.
- **FR-008**: The system SHOULD maintain consistent speaker coloring for the duration of a session;
   colors MAY reset when a new session begins.
- **FR-009**: The system SHOULD support language-agnostic transcription where possible; if limited,
   supported launch languages MUST be documented. [NEEDS CLARIFICATION]
- **FR-010**: The solution MUST operate primarily on the user’s desktop (Windows) without requiring
   an external account; if remote services are used, this MUST be clearly disclosed and optional.
   [NEEDS CLARIFICATION]
- **FR-011**: The transcript MUST be readable on standard desktop displays; consider basic font size
   and color-blind safe defaults. [NEEDS CLARIFICATION: accessibility requirements]
- **FR-012**: No persistence or export in v1. Transcripts are session-only and cleared when the
   app exits or the session is reset.

- **FR-013**: Diarization MUST run locally in near real-time using incremental clustering (no cloud
   dependency). Speaker attribution lag SHOULD be minimal (target ≤ 1s behind transcription) and
   MUST not block transcript updates.

- **FR-014**: ASR MUST use Whisper base or small models in v1 (local, no cloud). Default = small;
   users MAY switch to base via settings. Document accuracy/latency trade-offs.

- **FR-015**: On Windows, Listening uses the default microphone with shared access; audio is
   normalized to 16 kHz mono for transcription. The app MUST not seize exclusive device control.

- **FR-016**: UI for v1 MUST use Qt Quick/QML on Windows. Provide a main window with a live
   transcript view and consistent per‑speaker colors. Favor accessible color choices and adjustable
   font size.

*Example of marking unclear requirements:*

- Data handling: Are audio and transcripts strictly local-only, or can cloud processing be enabled
  by the user? [NEEDS CLARIFICATION]
- Accessibility: Required minimum contrast and adjustable font sizes? [NEEDS CLARIFICATION]

### Key Entities *(include if feature involves data)*

- **Transcript Session**: A single Listening session with a start/stop time and an ordered list of
   speaker-attributed segments.
- **Speaker (anonymous)**: An index-based label (Speaker N) representing a distinct voice in the
   current session; includes a color association for UI rendering.
- **Transcript Segment**: A piece of recognized text with timestamps and an associated Speaker label;
   used to render lines in the transcript view.

---

## Review & Acceptance Checklist

GATE: Automated checks run during main() execution

### Content Quality

- [ ] No implementation details (languages, frameworks, APIs)
- [ ] Focused on user value and business needs
- [ ] Written for non-technical stakeholders
- [ ] All mandatory sections completed

### Requirement Completeness

- [ ] No [NEEDS CLARIFICATION] markers remain
- [ ] Requirements are testable and unambiguous  
- [ ] Success criteria are measurable
- [ ] Scope is clearly bounded
- [ ] Dependencies and assumptions identified

---

## Execution Status

Updated by main() during processing

- [ ] User description parsed
- [ ] Key concepts extracted
- [ ] Ambiguities marked
- [ ] User scenarios defined
- [ ] Requirements generated
- [ ] Entities identified
- [ ] Review checklist passed

---
