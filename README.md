# sferic_scape
A C++ synthesizer for atmospheric and environmental sounds — thunder, wind, ocean, rain. No samples. Everything generated from physical models driven by real-world parameters. Discharge current, wind speed, wave height, distance.

***sferics*** — <small>the broadband electromagnetic bursts emitted by lightning.</small>
<br>
***scape*** — <small>the rest of it.</small>

Early in development. Thunder is the only present and *near-complete* module so far. The analysis path in the codebase is the working side.

The analysis path takes a real recording, extracts a time-varying spectral envelope, and reshapes filtered noise to match it. The physics path derives the same envelope from equations — discharge current, bolt geometry, listener distance — no recording needed. Both paths produce audio the same way; what changes is where the envelope comes from.

### *Long Term Goals*

#### Soundscape Composer
Layer multiple synthesized sources simultaneously — thunder, wind, ocean, rain — each module independent and composable. Sources picked manually or procedurally: storm distance drifting over time, wind speed varying, lightning strikes at randomized intervals and bearings. Density, spatial distribution, and timing all controllable or left to run on their own.

Future additions to the source library: birdsong (formant synthesis from vocal tract geometry), insects (stridulation rhythm models), rivers (turbulence spectrum from flow rate and channel width), fire (turbulent combustion noise, crackle as a Poisson process). The goal is an environment that generates itself from physical parameters — not a playlist of samples.

#### Live Output
Stream directly to a system audio output (WASAPI / JACK / CoreAudio) rather than writing to file. The buffer-passing architecture already supports this — *just* need to add a real-time backend layer.

Could also stream over a network as a continuous audio feed. Linked graphical visualizations driven from synthesis state — running spectrogram, storm position, parameter readouts — are a natural extension. An OSC or WebSocket control surface could let external tools drive parameters in real time: close a storm, increase wind speed, trigger a strike.

#### DAW Plugin (VST3 / AU)
Every physical parameter — discharge current, bolt distance, terrain openness, wind speed — becomes an automatable lane in the host. Strike events triggered from MIDI notes. Modulation from the DAW (LFOs, envelope followers, automation clips) mapped to any synthesis parameter.

Multi-output routing: separate buses for crack, rumble, wind, rain so each layer can go through its own processing chain. Sidechain possibilities: drive lightning intensity from a transient, modulate storm distance from an automation clip. Preset system for environment snapshots — coastal storm, distant mountain thunder, forest rain.

#### Spatial Rendering
The propagation model already takes real 3D geometry as input — distance, altitude, cylindrical spreading, frequency-dependent absorption per segment. Single sources are already positioned correctly in acoustic terms.

What's missing is the listener-side spatial layer: HRTF or ambisonic encoding so sources carry direction, not just distance. With that in place a moving listener is supported — the soundscape updates continuously as position changes. Multiple sources distributed across a scene, each rendered at its own geometry, producing a physically coherent spatial mix. The end state: a complete procedural weather engine, physically synthesized and spatially correct for wherever the listener stands.

---

<details>

*<summary><strong>Dev Notes</strong></summary>*
This is my 3rd attempt at a spectral synth engine. Started in Python — the results were lackluster and I hit the ceiling on performance quickly. C++ was the obvious move for this style of real-time, sample-level work.

The first C++ design was built around the full Spectral Modeling Synthesis pipeline — Xavier Serra's framework from the 90s. The idea: analyze a recording, extract sinusoidal partials and track them over time, compress everything into a compact parameter set, then resynthesize with an oscillator bank and a noise generator. That pipeline was fully implemented and tested.

The oscillator bank was never really intended for modules that are just noise. It was designed for sounds where clear oscillation is present — where you can actually track a frequency over time. Thunder is broadband transient noise; there are no stable partials worth tracking. The oscillator bank was adding energy rather than reconstructing it, so the residual the noise generator was supposed to cover was actually louder than the original. The deterministic component contributed nothing.

Dropped it entirely. The stochastic path alone — extract a spectral envelope, regenerate noise shaped by that envelope — produces better results and is much simpler.
</details>

## Thunder Module
Implements the full acoustic chain from lightning discharge to listener.

### [`LightningModel`](src/thunder/lightning_model.cpp) — Source
generates a tortuous channel from bolt geometry. Each segment carries a physical spectral peak from Few's (1969) formula:

$$f_{\text{peak}} \approx 0.63 \cdot c_0 \cdot \sqrt{\frac{P_0}{E_l}}$$

| Symbol            | Meaning                                    | Typical value        |
|-------------------|--------------------------------------------|----------------------|
| $f_{\text{peak}}$ | Spectral peak frequency                    | —                    |
| $c_0$             | Speed of sound in air                      | 343 m s⁻¹            |
| $P_0$             | Ambient atmospheric pressure               | 101 325 Pa           |
| $E_l$             | Acoustic energy per unit length of channel | ~10⁶ J m⁻¹ at 100 kA |

That gives **~65 Hz** at **100 kA** at the source. The received peak at **5 km** is **~35 Hz** after propagation.

The rumble is not reverberation — it's intrinsic to the source geometry. A bolt is a tortuous channel up to **5 km** long; each segment fires simultaneously but sound from the far end arrives seconds later. The extended decay is just geometry.

### [`PropagationFilter`](src/thunder/propagation_model.cpp) — Propagation
cylindrical spreading with frequency-dependent absorption $Q(f) = 5.2 \cdot f^{1.58}$ <small>(Hong et al. 2023)</small>
<br>High frequencies attenuate sharply with distance — distant thunder is always bass-heavy regardless of the source spectrum.

### [`EnvironmentProcessor`](src/thunder/environment_model.cpp) — Environment
ground reflection, feedback delay network for terrain scattering, wind modulation.

### [`CrackAnalyzer`](src/thunder/crack_analyzer.cpp) — Crack analysis
extracts a ``StochasticModel`` from the crack portion of a real recording.
<br>Onset detection → window extraction → multi-resolution STFT → feature classification (Peak / Clap / Rumble).
<br>The model can replace the physics envelope for the crack while the rumble stays physics-generated — a hybrid path between measured and synthetic.

#### [`docs/THUNDER_PAPERS.md`](docs/thunder_papers.md)
For physics grounding and paper notes for each layer

## Reference Points
['TORONTO 1am' by TRP](https://freesound.org/people/TRP/sounds/616992/)
<br>Used as the main comparison target sample throughout development.

[SYNTH_616992.wav](data/SYNTH_616992.wav)
<br>the analysis path output from that recording.

> GitHub doesn't support inline audio players in READMEs. The link above will download the file.

**Still missing:**
- Smoother envelope interpolation between stochastic frames
- Stereo model needs work (the infrastructure is there, it just doesn't work)
- Artifacts are clearly present
- High-end frequencies are incorrect
- Lots more...

## Pipeline
### Physics path — <small>physical parameters in, audio out</small>
<table>
<tr><td align="center"><b>Physical Parameters</b></td><td></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Source Model</b></td><td><sub>bolt geometry · wind field · wave spectrum</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Spectral Envelope</b></td><td><sub>time-varying noise shape from physical equations</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Noise Synthesis</b></td><td><sub>overlap-add · frequency-domain shaping</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Propagation</b></td><td><sub>distance attenuation · Q(f) absorption</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Environment</b></td><td><sub>ground reflection · terrain FDN · wind modulation</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Output</b></td><td></td></tr>
</table>

### Analysis path — <small>learn from a real recording. Output feeds back into Physical Parameters</small>
<table>
<tr><td align="center"><b>Recording</b></td><td></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>STFT</b></td><td><sub>short-time Fourier transform</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Stochastic Model</b></td><td><sub>extract time-varying spectral envelope</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Noise Synthesis</b></td><td><sub>regenerate noise matching the extracted shape</sub></td></tr>
<tr><td align="center">↓</td><td></td></tr>
<tr><td align="center"><b>Output</b></td><td></td></tr>
</table>

## What's next
Fix the remaining thunder issues. Then expand to other sources. Each module will follow the same pattern — physics model produces a spectral envelope, noise synthesis renders it, propagation and environment shape it for the listener's position.

| Module | Physical basis                                                |
|--------|---------------------------------------------------------------|
| Wind   | von Kármán turbulence spectrum, aeolian tones from obstacles  |
| Ocean  | Pierson-Moskowitz wave spectrum, surf break and foam noise    |
| Rain   | drop size distribution, surface strike resonance, canopy drip |
| Fire   | turbulent combustion noise, crackle as Poisson process        |
