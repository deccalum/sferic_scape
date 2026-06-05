# sferic scape
A synthesizer for atmospheric and environmental sounds — 
*thunder, wind, ocean, rain.* No samples. Everything 
generated from physical models driven by real-world parameters. 

## Build & Run

Create `.env` or rename `env.example`  and:
```
cmake build && build/sferic
```

### Compare in / out 
```
python3 tools/compare.py  data/out/<name.png>/
python3 tools/diagnose.py data/out/<name.png>/   
```

## Relevant Notes & Details

**SpectralEnvelope** [`src/analysis/spectral_envelope.h`](src/analysis/spectral_envelope.h)  
Extracts a spectogram of given source. Same
information that would be visible in a 
spectrogram. Everything that comes after 
reads from this. Acts as a separation stage 
between recording and **ParametricModel**.
Can output to audio for reference — it is 
*the best reconstruction from spectral 
data possible*.

**ParametricModel** [`src/analysis/parametric_model.cpp`](src/analysis/parametric_model.cpp)  
What the SpectralEnvelope becomes after 
analysis. Instead of thousands of 
unlabelled numbers, you get named 
quantities — attack time, decay shape,
which frequency bands carry the most 
energy, how the spectral character shifts
over the course of the sound.

### Next Focus Point
The model is *source-adaptive*: band 
count and positions vary per recording.
To use it procedurally the parameters 
need a fixed canonical form — fixed band
slots that mean the same thing across 
sources. Running the extractor over 
*x amounts* of recordings and collapsing 
to a median band structure gives that form.

### Long Term Goals
#### Soundscape Composer
Layer synthesized sources simultaneously 
— thunder, wind, ocean, rain 
— each module independent and composable. 
Sources picked manually or procedurally: 
storm distance drifting over time, wind s
peed varying, lightning strikes at 
randomized intervals and bearings. 
Density, spatial distribution, and timing 
all controllable or left to run on 
their own.

#### Live Output
Stream directly to a system audio output 
rather than writing to file. The 
buffer-passing architecture already 
supports this — *just* need to add 
a real-time backend layer.

Could also stream over a network as a 
continuous audio feed. Linked 
graphical visualizations driven from 
synthesis state — running spectrogram, 
storm position, parameter readouts 
— are a natural extension. 
An <samp>**OSC**</samp> or 
<samp>**WebSocket**</samp> control 
surface could let external tools drive 
parameters in real time: 
close a storm, increase wind speed, 
trigger a strike.

#### DAW Plugin
Every physical parameter — discharge current, 
bolt distance, terrain openness, wind speed — 
becomes an automatable lane in the host. Strike 
events triggered from MIDI notes. Modulation 
from the DAW *(LFOs, envelope followers, 
automation, sidechains)* mapped to any 
synthesis parameter.

**Multi-output routing:** separate buses for 
crack, rumble, wind, rain so each layer can 
go through its own processing chain. Sidechain 
possibilities: drive lightning intensity from a 
transient, modulate storm distance from an 
automation clip. Preset system for environment 
snapshots — coastal storm, distant mountain 
thunder, forest rain.

#### Spatial Rendering
The propagation model already takes real 3D 
geometry as input — distance, altitude, 
cylindrical spreading, frequency-dependent 
absorption per segment. Single sources are 
already positioned correctly in acoustic 
terms.

What's missing is the listener-side spatial 
layer: <samp>**HRTF**</samp> or ambisonic 
encoding so sources carry direction, not 
just distance. With that in place a moving 
listener is supported — the soundscape 
updates continuously as position changes. 
Multiple sources distributed across a 
scene, each rendered at its own geometry, 
producing a physically coherent spatial 
mix.

**End state**  
a complete procedural weather engine, 
physically synthesized and spatially 
correct for wherever the listener stands.
