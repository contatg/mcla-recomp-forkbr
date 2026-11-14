# Testing the Android ARM64 build

This experimental build uses the extracted files from the user's own copy of
*Midnight Club: Los Angeles Complete Edition* for Xbox 360 (Title ID
`545407F8`). The APK contains the ARM64 program; game data remains outside it.

## Installation without root

1. Install the ARM64 APK produced by `scripts/build_android_apk.sh`.
2. Extract the game files into a folder accessible to Android's document
   picker, such as `Download/MCLA-extracted/`. Select that subfolder rather
   than the `Download` root, which cannot be granted to an app on some Android
   versions.
3. Open **MCLA Recomp**, tap **Select game folder**, and choose the extracted
   folder. Wait for the copy to finish; it needs about 6.2 GB plus free space
   for the source and private copy.
4. After import, tap **Play**. Later launches show the driver controls and
   **Play** before starting the game.

The document picker grants access only to the selected folder; broad storage
permission is not required. Confirm the selection when Android asks. If access
is denied, choose the folder again. On Android 11 or newer, select the
`MCLA-extracted` subfolder instead of the `Download` root.

The 13 files must be directly inside the selected folder: `default.xex`, four
`xarchive_*.rpf` files, six `.bik` videos, `nxeart` and
`root_directory_padding.pad`. The launcher checks presence and nonzero size,
including after an interrupted copy, but does not verify hashes. A closed `.zip`
or `.iso` is not accepted by the picker.

## Direct copy with root

The private destination is:

```text
/data/user/0/org.mcla.recomp/files/game/
```

`/data/data/org.mcla.recomp/files/game/` points to the same location on the
tested device. Install and open the APK once, then copy the **contents** of the
extracted folder into `game/` with read permission for the app UID. A normal
file manager cannot access `/data/user/0` without root, so use the document
picker instead when root is unavailable.

Do not clear app data if you want to keep the imported game, settings and
saves. Updating with `pm install -r` preserves app data. Private logs are in
`files/logs/`; shader caches are under `files/mcla/cache/shaders/`.

On Android 10 and newer, logs are also exported to
`Documents/MCLA/logs/` through MediaStore when the launcher opens, during
gameplay, and when the game pauses or closes. Shared copies use `.txt` names,
while private originals retain `.log` names. The folder is created by Android
without broad storage permission. If the game closes before an export, reopen
the launcher to export the private log. The final filename migration still
needs runtime validation. See [Android shared storage](https://developer.android.com/training/data-storage/shared/media).

The development device had a prepared folder under
`/storage/emulated/0/Download/MCLA-extracted/`; the files matched the local
extraction byte for byte. Existing private-copy detection was tested, but a
clean-profile picker import remains unverified.

## Input support

Single-gamepad input is the only supported and validated in-game input method
at present. Two-gamepad/two-player input is not supported at this time.
Keyboard and mouse do not work as in-game controls in this Android port.
Xbox-style touch controls are included in the 0.1.24 Snapdragon APK as an
experiment, but not yet validated in gameplay; a gamepad remains the only
validated method. The overlay hides when a gamepad is connected; without a
gamepad it hides after six seconds without interaction and returns on touch.

Local Android touch prototypes implement a synthetic Xbox-style controller:
two sticks, D-pad, A/B/X/Y, LB/RB, LT/RT, LS/RS, Back and Start. The overlay
is present only in the game activity. A detected physical gamepad hides it
and clears held touch input; unplugging the gamepad brings it back. With no
gamepad, it hides after six seconds with no fingers down, then the next touch
reveals it. The maintainer confirmed the overlay works in gameplay on the
tested phone, but it remains experimental: report layout, dead zones and
missing buttons from other devices. The Snapdragon builds ship this overlay;
it is still not a supported alternative to a gamepad. Test APK variants share a
package ID, so two of them cannot coexist on one device.
The launcher in newer local prototypes has **Customize touch controls**:
drag controls in its landscape preview, adjust the overall size, hide the
toolbar to reach controls underneath it, and use **Reset layout** to restore
the defaults. The layout is stored in app-private Android preferences and
survives `pm install -r`. Sticks recenter where the finger initially touches
their hit area; their visible thumb moves with the finger and returns to
neutral on release. The original six-second/gamepad auto-hide rules remain.

The launcher uses a dark card-based layout with separate Game files,
Graphics, Extras and Launch sections. The same import, driver selection,
shader-pack, touch-editor and Play actions remain available; the appearance
does not change Vulkan behavior.

## Snapdragon 680 performance

Choose a profile under **Graphics** before pressing **Play**. Automatic selects
**Performance** when Android 12 or newer reports `SM6225` as its SoC model. The
profile can also be selected manually.

| Profile | Graphics settings |
| --- | --- |
| Standard | Uses the regular game and Vulkan settings. |
| Performance | Disables real-time shadows, MSAA and blur. Uses simpler car shadows, 2× anisotropy, shorter traffic distance and fewer pedestrians and parked cars. |
| Maximum performance | Reduces city detail further, turns off anisotropy, removes pedestrians and parked cars, and updates reflections every other frame. Uses a 16:9 guest image at 720 lines, so wide displays can have side bars. |

Both performance profiles limit Vulkan pipeline creation to two workers, shorten
waits for queued pipelines during occlusion queries, and spend less CPU time
waiting for the frame deadline. Maximum performance also compiles eligible
occlusion-query pipelines in the background. During compilation, the query can
temporarily treat draws that have no pipeline yet as hidden.

When a custom Turnip driver is selected, **Automatic (profile)** disables
`syncdraw` for both performance profiles. Select `syncdraw` manually if the
driver shows flickering wheels or rear lights. Settings already saved in
`mcla.toml` have priority over the profile defaults.

## Measuring FPS

Drive the same route for at least two minutes per profile with the same driver.
Run the route once before measuring to give the shader cache time to warm up.
The target on Snapdragon 680 is at least 20 new guest frames presented per
second while driving. This still needs testing on the device.

The Android log reports both new guest frames successfully presented by the
host and game updates, with a 95th-percentile frame interval for each. Repaints
of the same guest image do not count as new frames. Both performance profiles
turn off the on-screen FPS overlay by default and keep the log active.
The optional `mcla_fps_overlay` and `mcla_fps_log` settings are in `files/mcla.toml`.

## No game sound until recording starts

Some devices keep the AAudio low-latency output stream inaudible until another
stream opens the normal mixer, so the game is silent until screen recording
(or another app's audio) starts. The current Android build opens the normal
mixer path by default, and the activity requests audio focus so another app's
playback is paused instead of leaving the game ducked.

The launcher's **03 Extras** card has an audio mode button:
**Audio: compatibility (normal mixer, always audible)** is the default, and
**Audio: low-latency (best performance, may be silent)** selects the old
AAudio fast path. The choice is stored in app-private preferences and applied
as the `REX_MCLA_AUDIO_LOW_LATENCY` environment override before the runtime
starts; the same value can be set as `mcla_audio_low_latency` in
`files/mcla.toml`. A reported frame-rate difference between the two modes has
not been measured in a controlled run, and audio latency was not measured.

## Optional Vulkan driver

The default is the system Vulkan driver. In the launcher, tap **SELECT TURNIP
(.ZIP)**, choose a trusted ARM64 package containing
`libvulkan_freedreno.so`, wait for the import message, and tap **PLAY**. The
driver is copied to `files/gpu-drivers/`. **USE SYSTEM DRIVER** disables the
selected Turnip path without deleting the original ZIP.

The selected ZIP contains native code that runs inside the app. The importer
checks ELF ARM64 format and size limits, but that does not make an untrusted
driver safe. Import packages only from sources you trust.

Turnip compatibility depends on the Adreno GPU, Android version, kernel and
package version. An incompatible combination can cause a black screen, Vulkan
failure or worse performance. If the custom driver fails to open, the loader
falls back to the system driver. Check `files/logs/mcla_*.log` for
`AdrenoTools opened Vulkan for selected Turnip`, the actual `Vulkan device`
line, or `AdrenoTools rejected selected driver`.

The system driver is the artifact-free option without a diagnostic switch on
the tested Samsung Galaxy S23. Turnip 8G2-OneUI and Standard
v27.0.0-20261107 produced rear-light flashes in the user's tests; the tested
Turnip 27 build needs `TU_DEBUG=syncdraw` to avoid the observed rear-light and
wheel artifacts. System Vulkan was slower in the tested sessions. The spoofed
`realme/RMX5070` string in old logs is not the phone model. Thermal throttling
was observed, so FPS comparisons require similar temperature and cache
conditions.

### Turnip driver debug switches (diagnostic APK)

To isolate a Turnip rendering artifact, put exactly one line in the app-private
`files/driver.env` before starting the game, for example `TU_DEBUG=noubwc`.
The Android activity applies it before SDL loads Vulkan and logs the applied
value under the `MCLA` Android logcat tag. Supported values are `noubwc`,
`syncdraw`, `flushall`, `nolrz` and `sysmem`; do not combine them in one run.
Restart the game after changing or removing the file. Delete `driver.env`
after testing to return to normal driver behavior. These switches may reduce
performance; compare the same scene and keep the shader and pipeline caches.

`TU_DEBUG=syncdraw` removed both rear-light and wheel flashes on the tested
Adreno 740, while `noubwc` and `nolrz` did not. This does not establish that a
wait once per frame is sufficient: `syncdraw` also affects draws within a
frame. APK 0.1.13 adds an experimental, disabled-by-default Vulkan test. With
`driver.env` removed, set `vulkan_sync_at_frame_start = true` and
`vulkan_sync_at_frame_start_frames = 0` in `files/mcla.toml` and restart the
game. `0` waits for all submitted work; values up to `3` permit that many
prior *submissions* in flight, not necessarily whole frames. The GPU log
reports wait counts and total microseconds in roughly two-second intervals.
On the tested A740, this frame-start wait did **not** remove either artifact.
APK 0.1.14 adds `vulkan_sync_at_submission = true` as a separate opt-in test:
wait for the preceding submission before beginning the next one. This does
not serialize draws within a submission. On the tested A740 it removed both
artifacts, but reduced host-presentation FPS from about 29 to 15 in the tested
sessions. With `vulkan_submit_on_primary_buffer_end = false`, the artifacts
returned despite the wait. APK 0.1.15 adds a separate experimental option,
`vulkan_sync_after_primary_buffer_end = true`, to wait only for submissions
closed at eligible guest primary command-buffer boundaries. Session 051
confirmed that this selective wait did not remove the artifact: median host
presentation was 22.5 FPS, with about 40 waits and 1.39 seconds blocked per
two-second interval. The usable workaround remains `TU_DEBUG=syncdraw` with
threaded submission enabled (session 040: 28.8 FPS median, no artifact).
Test each mode separately, without `driver.env`, and restart after changing
`mcla.toml`.
`vulkan_threaded_submission_split_draws = 1` caused the loading screen to
stall while the presenter repeatedly showed the same image; do not use it as
an FPS measurement or a fix. Remove all experimental settings and restart to
restore the defaults.

The public reproduction matrix and limitations for the Turnip A740 artifact
are in [turnip-a740-rendering-artifacts.md](turnip-a740-rendering-artifacts.md).

## Optional shader pack

The Android build does not ship the precompiled shader pack. If you have
`545407F8.shaderpack` from the same graphics-code version, tap **SELECT SHADER
PACK (.SHADERPACK)** in the launcher and choose the file. It is copied to
`files/shaderpack/545407F8.shaderpack`; the game adds the shaders and pipelines
it lacks at the next start. The pack holds no game data, only fingerprints and
pipeline descriptions, and is optional. A pack made for another game or
version is rejected with a `shader pack:` line in `files/logs/mcla_*.log`.

The user reports a small FPS loss with the pack and the system driver; no
controlled frame-time comparison is available yet. A 2026-11-09 log showed
1,721 pipeline descriptions added, but no shaders rebuilt. Compare the
pipeline creation burst and the car-load hold with and without the pack, one
change at a time. The pack is not required for gameplay.

## Sharper picture with the present filter

The guest image is drawn at the guest video mode (1560x720 on a 19.5:9 display
with the Android default) and scaled to the phone's screen by the presenter,
which uses a bilinear filter. The build also carries FidelityFX's Contrast
Adaptive Sharpening and FSR 1 as pixel shaders: they need neither
FidelityFX's source nor its runtime, and they are compiled into the Android
build. They are off by default because each is another pass over the whole
picture, and the GPU is the limit on this phone. To try them, add a line to
`files/mcla.toml`:

```toml
present_effect = "cas"
```

The options are `bilinear` (the default), `cas` and `fsr`. `cas` sharpens the
upscaled image, `fsr` upscales it with EASU and sharpens it with RCAS (more
passes, more GPU time). The temporal upscaler (`fsr2`, `fsr3`) needs
FidelityFX's runtime, which this build does not have, so it is not offered.
Compare the frame times with and without the filter: a full-screen pass at
2340x1080 is not free on a thermally limited GPU.

## Measuring the occlusion-query pipeline wait

The 2026-11-09 driving profile without a shader pack showed 14.223 average
presented FPS and a 110 ms p95 frame interval while the GPU was usually
limited to 220–295 MHz. It created only 20 pipelines after startup and logged
no slow occlusion-query wait. Focus the next comparison on CPU time, thermal
limit and frame intervals rather than assuming pipeline creation is the cause.

An experimental build can set `mcla_fence_sleep_us = 50` in `files/mcla.toml`
to sleep for up to 50 microseconds after every 64 GPU fence polls. The default
is 0, which preserves the tested behavior. This is a CPU/heat hypothesis, not
a verified speedup; compare with 0 in the same scene and stop if it introduces
new visual artifacts or longer stalls.

From Termux on the tested S23, summarize a private native log without copying
game data or logs into Git:

```sh
su -c 'cat /data/user/0/org.mcla.recomp/files/logs/mcla_016.log' | \
  python3 -I scripts/analyze_android_pipeline_log.py -
```

Replace the log number with the run being examined. The script counts pipeline
creations and direct texture loads per second, and prints logged occlusion
waits. A burst when Vulkan storage is first loaded is startup preparation;
the useful driving comparison is the later burst in the same scene. Keep the
driver, shader-pack state, game scene and thermal limit recorded for each run.
The script does not measure frame times or prove that a pipeline caused an FPS
drop.

A draw inside an occlusion query waits for the pipeline it needs: one created
right away can take a long time in the driver, and one queued by an earlier
draw is waited for up to 500 ms. The build logs both times when they reach
10 ms:

```text
VulkanPipelineCache: the pipeline of a draw inside an occlusion query took N ms to create
Vulkan: a draw inside an occlusion query waited N ms for the pipeline it needed
```

Both paths can be bounded without a new build, by adding a line to
`files/mcla.toml` (the file the runtime reads at every start):

```toml
vulkan_occlusion_query_pipeline_wait_ms = 16
vulkan_occlusion_query_async_pipelines = true
```

The first line caps the wait for a pipeline another draw queued; `0` leaves
the draw out right away. The second creates those pipelines in the background
too, leaving the draws out until they are ready, which the occlusion query
counts as hidden: it trades the query's accuracy for a frame that does not
wait for the driver. Remove a line to go back to the default. Compare one
setting at a time, and keep the same driver, temperature and cache state.

## Rebuilding the APK

No build is performed by this documentation page. The source recipe is in
[android-build.md](android-build.md), and CI builds the patched SDK and host
stub without game data. The APK includes AdrenoTools and its hooks, but no
third-party Turnip driver.

## v0.1.52 — transição para a cidade

A hipótese desta versão é que a espera da thread principal acompanha o
trabalho de outra thread do guest. A v0.1.51 registrou cerca de uma
atualização por segundo durante a transição e leituras de 4.112.573 e
4.562.237 bytes do cache Vulkan em duas sessões Normal. A imagem verde
ainda precisa de diagnóstico do draw.

- `mcla_city_diagnostics=false` desativa a amostragem de PC e o rastreio
  detalhado de esperas no `mcla.toml`. Os logs da v0.1.51 continuam ativos.
- `[hotguest]`: amostra o PC ARM64 da XThread de trabalho com maior
  consumo de CPU no último segundo parado. O endereço `sub_*` é a
  função PPC mapeada pela tabela de crash. `outside_guest_code` é uma
  amostra fora do mapeamento. Não classifique função desconhecida pela
  porcentagem de amostras.
- `[guestwait2]`: registra esperas de pelo menos 100 ms. `address_kind`
  informa se o valor é endereço guest ou handle. O tipo 2 é evento,
  6 é mutex, 8 é semáforo, 12 é thread e 13 é timer. `signaler=0`
  indica ausência de um sinal atribuível no intervalo. Um TID registrado
  é o último sinal instrumentado durante a espera. Em espera múltipla
  pendente, a linha periódica mostra o primeiro objeto do grupo.
- `[mem]`: RSS do processo e alocações de render targets a cada 5 s.
  A contagem cobre o cache residente de alvos.
- `[pipeline-cache] saved_bytes=`: registra a gravação bem-sucedida
  do cache. A linha `Loaded ... bytes of VkPipelineCache data` continua
  indicando o carregamento. Já existe gravação em background após drenar
  a fila (2 s sem novas criações e intervalo mínimo de 10 s) e no
  encerramento normal se houver alterações.

| Opção no SD680 | Normal | Performance |
| --- | --- | --- |
| TU_DEBUG automático para Turnip | `syncdraw` | Sem override |
| Anti-alias, blur e sombras | Configuração padrão do jogo/preset | Desligados, com caminho rápido de sombra de carro |
| LOD cidade / tráfego | Configuração padrão do jogo/preset | 0,75 / 0,5 |
| Distância de tráfego / pedestres / carros estacionados | Configuração padrão do jogo/preset | 250 / 0,5 / 0,5 |
| Anisotropia | Configuração padrão do jogo/preset | 2 |
| Espera de pipeline em occlusion query | 500 ms por padrão | 8 ms por padrão |
| Janela de espera ativa por quadro | 1.500 µs por padrão | 300 µs por padrão |
| Compiladores Vulkan | 2 | 2 |
| Dynamic rendering / upload direto / pausa em frames incompletos | Desligados na compatibilidade Adreno 610 | Desligados na compatibilidade Adreno 610 |
| Hook de fence em `render_perf.cpp` | Mesma configuração | Mesma configuração |

Teste cada perfil por até 90 s na cidade ou até entrar em uma corrida.
Exporte em **Extras → Exportar diagnóstico** logo depois de cada teste,
salve as duas cópias com nomes diferentes e informe se houve imagem verde,
áudio e corrida. Inclua as linhas `[stage]`, `[hotguest]` e
`[guestwait2]` desde a transição.
