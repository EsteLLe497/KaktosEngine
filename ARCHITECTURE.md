# Kaktos Engine architecture

The Win32 application remains event driven. Expensive work must not run from
`WM_PAINT` or input handlers.

## Runtime boundaries

- `BackBuffer`: owns reusable GDI back buffers for the editor and preview.
- `GdiFontCache`: owns shared fonts used by high-frequency drawing paths.
- `AsyncTextWriter`: coalesces autosave snapshots and writes them on one worker.
- `Persistence`: atomic UTF-8 replacement, project IDs, and save checksums.
- `StreamingAudioPlayer`: compressed BGM streaming backed by miniaudio.
- `Scenario`: parses and serializes the scenario document.
- `NovelRuntime`: coordinates playback and editor state. New I/O or rendering
  ownership should be added to a focused service instead of this class.

## Persistence rules

- Project and save formats include `format_version` and `project_id`.
- Paths inside a project are relative to the project/assets directory.
- Legacy absolute paths containing an `assets` segment are migrated when the
  same relative asset exists in the opened project.
- Writes go to a temporary file, are flushed, and replace the destination while
  retaining a `.bak` recovery file.
- Default player saves live under `%LOCALAPPDATA%\KaktosEngine\Saves\<project-id>`.

## Performance rules

- Scenario validation is cached until the document or asset index changes.
- BGM uses compressed streaming. SE, voice, and previews decode outside the UI
  thread; XAudio2 voice creation remains on the UI thread. Their decoded cache
  memory is capped at 96 MiB.
- Runtime updates use a 16 ms timer and text reveal catches up from elapsed time.
- Autosave waits 800 ms after the latest edit and coalesces pending snapshots.
