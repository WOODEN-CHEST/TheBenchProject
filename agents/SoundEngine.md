# SoundEngine (audio)

Files: `include/SoundEngine.h`, `source/SoundEngine.c`; the sound asset type is `source/SoundDefinition.c`
(loaded through `AssetManager_LoadSound`).

## Status: disabled until it is redesigned

- `AudioEngine_Construct1` always returns `ErrorCode_InvalidOperation` (see the `// TODO:` in
  `SoundEngine.c`). Nothing constructs the engine and nothing calls `AssetManager_LoadSound`, so no game code
  depends on audio yet.
- The owner decided (2026-10-17, plan decision D8 in `handoff/FIX_PLAN.md`) to keep this code dormant and
  redesign it when the game needs sound. **This file is the brief for that redesign.** Before writing code,
  ask the owner the open decisions at the end of this file and propose the header first: the owner reviews
  API shape before implementation.
- The old code stays in place as the starting point: its DSP, ramps, resampling and ring buffer are worth
  keeping (see "What survives").

## How the current engine works (for orientation)

- **Threads.** There is no WRThread. Mixing runs in raylib's stream callback (`AudioEngine_StreamCallback`)
  on miniaudio's device thread, while raylib's global audio lock (`AUDIO.System.lock`) is held. raylib asks
  for at most 512 frames per call. raylib's `AudioCallback` has no user-data pointer, which is why the engine
  pointer is a global (`_activeAudioEngine`). A `_Thread_local` flag lets API calls made from inside audio
  commands apply directly instead of being queued. raylib's own source for reference:
  `/home/wooden_chest/Workstations/Projects/raylib/6.0/src/raudio.c`.
- **Objects.** `AudioEngine` is a singleton owning the master `AudioTrack`, the raylib stream, a 2048-slot
  single-producer/single-consumer operation ring and `_publicSoundRegistry`. Tracks form a tree; each keeps a
  "public" copy of its modifier and subtrack lists for the main thread and an "active" copy for the audio
  thread, plus its playing sounds, scheduled commands and its own frame counter. A `GameSoundInstance`
  (public, heap) is wrapped by an internal heap `TrackSoundSlot`. `SampleProvider` holds ramped rate, volume
  and pan plus a modifier list. Modifiers (reverb, biquad low/high-pass, bit crusher) are caller-owned with an
  embedded vtable. `AudioCommand` = function pointer + `UserData`, run on the audio thread at a scheduled
  track frame or when a sound loops, ends or finishes its effect tail.
- **Mixing** is per output frame: drain the ring, walk the track tree (per sound: advance 4 ramps, handle
  loop/end and run commands, interpolated sample read, per-sound modifiers on a 1-frame buffer, equal-power
  pan and volume), apply track modifiers/pan/volume, hard-clip at the master.
- **Sample data.** `AssetManager_LoadSound` decodes the whole file to float (`LoadWave`/`LoadWaveSamples`).
  The mixer reads `GameSound._samples` every frame. Releasing the asset frees the samples and the
  `GameSound` struct immediately (`SoundLoaded_Destroy`).

## Verified defects (why it is disabled)

Line numbers are for commit `f045987` (before the disable stub was added).

1. **The registry only grows.** Entries are added at `SoundEngine.c:1646-1654` and removed only at shutdown
   (`:1419`); `RemoveSoundSlotAt` (`:932`) only detaches. Each one-shot leaks about 0.7 KB until shutdown,
   and `FindSoundSlot` (`:407`) scans every sound ever played. Removed subtracks leave `_publicSubTrackBuffer`
   (`:1528`), the only list `DeconstructTrack` frees (`:392`), so they are never freed. The leak is what makes
   "instance pointers stay valid" true: recycling requires handles, not pointers.
2. **Threads are not synchronized.** Through the `_Thread_local` paths the audio thread grows or walks the
   registry, public subtrack lists and modifier lists (`:1479, :1528, :1654, :1946, :2002, :2054`) while the
   main thread may do the same: a reallocation during iteration. `_publicSoundCount` is decremented with a
   separate load and store (`:941-945`), racing `atomic_fetch_add` (`:1663`). `AutomatedFloat_SetValue`
   (`:1874`) writes target, duration and current value non-atomically while `Advance*` (`:258-300`) rewrites
   them every frame. The ring itself (`Queue_Push`, `:213`) is a correct SPSC ring; the defect is the header
   promising calls from any thread.
3. **Sample data is freed with no handshake.** Releasing a sound frees the `GameSound` struct too, so the
   mixer's `_source` dangles (read at `:1126-1141`, `:497-530`). Nothing records which instances use a sound,
   and there is no way to stop them first.
4. **The audio thread allocates.** An allocating sort and buffer growth per scheduled command (`:469-485`);
   buffer growth in `AttachSoundSlot`, `AttachSubTrack`, `InsertModifier` (`:948, :965, :879`); the reverb's
   `Memory_Reallocate` (`:661-667`) on its first block (its delay line starts NULL), which aborts on failure;
   `Error_Construct3` messages on failure paths (`:473, :883, :913`) that are then discarded (`:1066,
   :1285`); and user commands may call `CreateSoundInstance` (two `Memory_Allocate`, `:1603`).
5. **Global engine pointer.** One static pointer is unavoidable for raylib's callback. But `SampleProvider_*`
   also reach the engine through it (`:421, :1934-2077`), and the callback reads it non-atomically (`:1266`).
6. **`clock()` used as wall time.** On glibc it is CPU time for the whole process; on Windows, wall time since
   start. Use `Stopwatch_GetTimestampNanoseconds` (WRStopwatch).
7. **Per-sound modifiers act on the whole track.** `SampleProvider_GetOwningTrack` (`:426-435`) never returns
   NULL for a sound, so Add/Remove/Clear on a sound (`:1943, :1999, :2051`) queue a *track* operation:
   clearing one sound's modifiers wipes the track's effects.
8. **Startup failures are ignored, and a raylib setting leaks.** Device/stream creation is never checked
   (`:1382-1393`). `SetAudioStreamBufferSizeDefault(512)` does nothing for a callback stream but permanently
   shrinks the buffer of every raylib `Music` stream created later.
9. **The audio thread can hang.** An end command that sets the state back to Playing without seeking loops
   forever (`:588-612`) while holding raylib's lock; the next raylib audio call on the main thread, including
   `StopAudioStream` at shutdown, deadlocks.
10. **The reverb goes silent during delay changes.** It zeroes its whole delay line whenever `DelaySeconds`
    changes (`:668-671`), so ramping the delay silences it for the ramp. One modifier attached to two
    providers advances its state twice.
11. **Partial failures leave bad state.** A failure after registration leaves objects registered but never
    attached (`:1663-1683, :1479-1506`). A command error aborts the rest of that frame's mix (a click).
12. **Wasted CPU.** The ring is polled every frame (`:1280`); `sinf`/`cosf` run every frame for every sound
    and filter (`:487, :737`).
13. **Style.** The header includes `raylib.h` without needing it; the `ISoundModifier` vtable has no
    `Destroy`; a typedef is named `BiQuadPassTypeEnum`; some header fields use `//` instead of `/** @brief */`.

## The redesign (approved direction)

**Threading rule.** Every public function is main-thread-only. The audio thread runs only engine code: no
user callbacks, no allocation, no logging (the Logger is not thread-safe), no `Error` objects.

- **Two queues, nothing else shared.** Commands main → audio (fixed-size records of about 64 bytes, capacity
  about 1024); events audio → main. The only other shared data is a published frame clock and timing stats.
- **Audio-side storage**, allocated once at engine creation: fixed arrays of voices, buses and effect states;
  a sound table (sample pointer, frame count and format, copied by value); bus mix buffers; a fixed-size
  min-heap of timed commands.
- **Main-side state.** Handle tables (slot index + generation), free lists, cached parameter targets for
  getters, per-voice `UserData`, per-sound reference counts and names.
- **Mixer** in 64-frame sub-blocks; the command queue is drained once per callback; timed commands apply at
  sub-block precision (about 1.3 ms at 48 kHz).
- **Recycling voices.** A voice slot returns to the main-thread free list only when the audio thread sends
  `VoiceEnded`. Every voice start produces exactly one such event (reason: Ended, Stopped, Killed, Stolen,
  Rejected). A one-shot is `PlaySound` with `outVoice == NULL`.
- **Release only after the audio thread confirms** (the same for effects and buses):
  1. `ReleaseSound` drops the sound's reference count to 0, marks it Retiring (new plays are rejected) and
     posts `RetireSound`.
  2. In its next block the audio thread fades out and kills that sound's voices, clears its table entry and
     pushes `SoundRetired` after its last read of the samples (release store).
  3. `AudioEngine_Update` (called once per tick from the main loop) pops the event (acquire load) and only
     then calls `AssetManager_ReleaseAsset`.

  Size the event queue to at least the total number of resource slots so these events can never be dropped;
  only diagnostic events may overflow, and those are counted.
- **Assets.** The engine owns its own `AssetUserID` (the project rule: whatever keeps asset data owns the
  asset user). Frames may still bulk-load sounds under their own user; `AcquireSound` just adds the engine's
  hold. The AssetManager needs no changes.
- **Errors.** The audio thread pushes fault events (queue overflow, timed-command heap full, stale handle);
  `AudioEngine_Update` logs each kind once, on the main thread.
- **Shutdown.** `StopAudioStream` and `UnloadAudioStream` take raylib's lock, so after they return the
  callback cannot run and everything can be freed directly. If the device stops calling back, releases stall
  and sounds stay loaded until shutdown: safe and bounded.

**API sketch** (real code puts one field per line and adds full docs):

```c
typedef struct AudioVoiceHandleStruct { uint32_t _index; uint32_t _generation; } AudioVoiceHandle; // + Bus/Sound/Effect handles
typedef struct AudioEngineSettingsStruct { uint32_t MaxVoices; uint32_t MaxBuses; uint32_t MaxSounds; uint32_t MaxEffects; uint32_t CommandQueueCapacity; uint32_t ScheduledCommandCapacity; } AudioEngineSettings;
AudioEngineSettings AudioEngineSettings_CreateDefault(void);                 // 64/16/256/32/1024/256
Error AudioEngine_Create(const AudioEngineSettings* settings, AssetManager* assets, AssetTypeID soundType, AudioEngine** outEngine);
Error AudioEngine_Deconstruct(AudioEngine* self);
Error AudioEngine_Update(AudioEngine* self, Logger* logger);                  // once per tick, main thread
uint64_t AudioEngine_GetCurrentFrame(const AudioEngine* self);
void  AudioEngine_SetCommandFrame(AudioEngine* self, uint64_t frame);         // stamps the commands that follow (scheduling)
void  AudioEngine_ClearCommandFrame(AudioEngine* self);
WREvent* AudioEngine_GetVoiceEndedEvent(AudioEngine* self);                  // raised inside Update
Error AudioEngine_AcquireSound(AudioEngine* self, const unsigned char* name, AudioSoundHandle* outSound);
Error AudioEngine_ReleaseSound(AudioEngine* self, AudioSoundHandle sound);
AudioBusHandle AudioEngine_GetMasterBus(const AudioEngine* self);
Error AudioEngine_CreateBus(AudioEngine* self, AudioBusHandle parent, AudioBusHandle* outBus);
Error AudioBus_SetVolume(AudioEngine* self, AudioBusHandle bus, float volume, double rampSeconds);
Error AudioBus_InsertEffect(AudioEngine* self, AudioBusHandle bus, AudioEffectHandle effect, uint32_t index);
Error AudioEngine_PlaySound(AudioEngine* self, AudioBusHandle bus, AudioSoundHandle sound, const AudioVoiceSettings* settings, AudioVoiceHandle* outVoice);
Error AudioVoice_Stop(AudioEngine* self, AudioVoiceHandle voice, double fadeSeconds);
Error AudioVoice_SetVolume(AudioEngine* self, AudioVoiceHandle voice, float volume, double rampSeconds);
bool  AudioVoice_IsAlive(const AudioEngine* self, AudioVoiceHandle voice);
Error AudioEngine_CreateReverb(AudioEngine* self, double maxDelaySeconds, AudioEffectHandle* outEffect); // delay line allocated here, on the main thread
Error AudioEffect_SetParameter(AudioEngine* self, AudioEffectHandle effect, AudioEffectParameter parameter, double value, double rampSeconds);
```

Effects become engine-owned and attach to one voice or bus at a time.

**What survives, what goes.**
- Moved mostly as-is (about 500 lines): ramp math (`:244-300`), resampling (`:162-192, :497-530`), loop/end
  math (`:588-635`) minus the command calls, the pan law (`:487`), the reverb/biquad/bit-crusher DSP with
  tail detection (`:639-877`) minus the reverb's reallocation, setter validation (`:1874-1922`),
  `GameSound_Construct1`, the SPSC ring logic (`:213-242`), and `SoundDefinition.c` unchanged.
- Rewritten: the object model, the mixer loop, the whole public API and the header.
- Removed: `AudioCommand`, `SoundInstanceInitializer`, the public `GameSoundInstance`, `SampleProvider` and
  `ISoundModifier`, the `_Thread_local` dual code paths, and per-track timelines (one engine clock instead).
- Size: about 2,900–3,200 lines (header ~700 with mandatory docs, main side ~1,000, mixer ~700, effects
  ~350, private header ~250, queue ~150).
- New files to announce: `source/SoundEngineMixer.c` (the audio-thread boundary: it must not include the
  Logger or call `Memory_*`), `source/SoundEngineEffects.c`, `source/SoundEngineInternal.h`.

**WRFramework gaps.** No lock-free queue and no atomics API (C23 `<stdatomic.h>` is ISO C and already used by
`SoundEngine.c`; it is lock-free for 8-byte types on GCC 13 and MinGW-w64 x86-64 — add
`static_assert(ATOMIC_LLONG_LOCK_FREE == 2)`). `WRObjectPool` does not fit (it grows by allocating,
zero-fills slots and has no generations). WRStopwatch and WREvent cover timing and notifications.

## Decisions to ask the owner before starting

1. **Which mixer:** (a) the custom redesign above; (b) a thin wrapper over raylib's Sound/Music (about 300
   lines; loses buses, smooth ramps, sample-accurate scheduling, effect tails and bus effects — raylib also
   mixes under a lock the main thread takes, and `UpdateMusicStream` decodes on the main thread); (c) a
   hybrid. Recommend (a) if buses/effects/ramps are wanted, which the existing feature set suggests.
2. **Thread synchronization:** (a) lock-free queues (recommended: ownership separated by construction,
   main-thread stalls cannot glitch audio); (b) one mutex shared with the callback (simpler, like raylib).
3. **Where the queue lives:** (a) a new SPSC queue module in WRFramework (recommended: reusable by a future
   loader or decoder thread); (b) private to SoundEngine using `<stdatomic.h>`.
4. **Who holds sample data:** (a) the engine owns an AssetUserID, `AcquireSound`/`ReleaseSound` by name,
   release deferred to `Update` (recommended); (b) a glue module so the engine knows nothing of assets; (c) a
   blocking "unregister and wait" (stalls the main thread; deadlocks if the device stops).
5. **User callbacks on the audio thread:** (a) remove them: timed commands at sub-block precision plus
   main-thread `WREvent`s (recommended; callbacks can hang the audio thread, defect 9); (b) a restricted
   callback API; (c) sample-accurate scheduling by splitting blocks (only if rhythm/music sequencing needs it).
6. **When all voices are in use:** (a) fail with an error; (b) steal the lowest-priority, oldest voice,
   falling back to an error (recommended). Also ask to add `ErrorCode_CapacityExceeded` to WRError (the old
   code misuses `BufferTooLarge`).
7. **Music:** (a) decode fully into memory (about 11.5 MB per stereo minute at 48 kHz; fine for effects);
   (b) raylib `Music` alongside the mixer (recommended when music is needed); (c) custom streaming voices
   (needs a decoder library; raylib exposes none).

## Verification plan

Add a test frame that plays a one-shot, a loop, a bus fade and reverb, and releases a sound mid-play. Run it
under the ASan build and separately under `-fsanitize=thread`. `Memory_GetCurrentAllocationCount` must stay
flat while audio plays steadily.
