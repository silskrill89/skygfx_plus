# Baseline — 2026-09-03 Reference Caps (`I:\art\expanded&enhanced\beta\caps`)

Target state for SkyGFX Plus, as captured 2026-09-03 16:59–18:00 (176 shots).
Sampled 12 shots across the range. Current Oct 29 build has regressed; this file is the written baseline fix lanes verify against.

## Per-Shot Table

| # | Filename | Time | Scene | Solids (bldg/veh/ped) | Coronas | Particles | Road/Ground | Water | Sky/Clouds | Lighting Mood | Draw Order |
|---|----------|------|-------|----------------------|---------|-----------|-------------|-------|------------|---------------|------------|
| 1 | mercury_5bOH5V05b2.png | 16:59 | Night, Commerce, downtown LS, driving | All solid, no see-through | Warm orange lamp coronas, large soft halo + anisotropic streak, translucent | None | Asphalt w/ double-yellow lines, warm pooled lamplight, mild sheen | — | Near-black, faint gradient | Warm sodium pools, dark ambient, mild bloom | Buildings/hedges occlude correctly |
| 2 | mercury_Px80AveC99.png | 17:05 | Dusk, rural road, RV + car | Solid | None near cam | Brown dust haze bottom-right, correctly translucent | Cracked dirt/gravel, texture reads | — | Strong orange-red gradient, pink cloud banding at horizon | Warm sunset, moderate contrast | Trees silhouette correctly over sky |
| 3 | mercury_zO6K8vCrsk.png | 17:09 | Morning 07:37, forest hill, car | Solid | Sun disc + soft yellow halo w/ radial streaks near horizon | None | Tan/khaki grass-dirt road | — | Pale cyan → pink horizon band | Soft pastel morning, low contrast | Grass/trees alpha cards clean |
| 4 | mercury_S78XXzoMm4.jpg | 17:09 | Morning 07:39, redwood forest, car | Solid (trunks, car) | None | None | Patchwork grass texture | — | Pink-grey w/ horizontal cloud streaks | Milky pastel, soft | Dry-brush alpha cards correct, car roof shows broad soft specular band |
| 5 | mercury_b6yi7xZ7aP.png | 17:19 | Dusk-dim city street, convertible + peds | Solid; car interior visible through open roof (alpha correct) | Small red traffic-light corona | None | Dark asphalt, yellow lines, contact shadow under car | — | Pale teal, dimming | Grey-blue dusk haze, distance fog fade | Street furniture + traffic lights layer right |
| 6 | mercury_M4cdD0qFA4.jpg | 17:20 | Los Flores underpass 18:37, red car | Solid; soft env sheen on bumper corners | None | None | Concrete + double-yellow, crosswalk bar | — | Pale turquoise top, scene in shadow | Strong desaturated teal-grey shadow grade, no black crush | Foreground yucca in front of fence/pole, correct |
| 7 | mercury_Bp6lhWLRCR.png | 17:44 | Night 00:05, damaged red car, burnout | Solid | Big warm coronas w/ star flares, bright but translucent | Grey-blue tyre smoke, correctly blended | Tan-brown lit asphalt, warm lamp pooling, faint wet sheen | — | Dark teal-navy | High contrast light pools, warm night | Smoke layers over road, under HUD |
| 8 | mercury_0jmHHz1GRC.jpg | 17:46 | Night 02:10, taxi scrape, Vegas-adjacent | Solid; strong flank specular streaks | Warm lamp corona upper-left | Orange additive sparks + skid trails | Concrete bridge railing lit warm; asphalt warm-tinted | — | Black sky | Warm sodium + neon accents (liquor-store cyan glow), strong contrast | Sparks render over car and ground |
| 9 | mercury_07Ktin8AY0.png | 17:49 | Deep night 04:10, Downtown LS, taxi | Solid | Green traffic-light corona (bright point), warm lamp crowns w/ streaks | None | Dark asphalt, faint patches, mild distance fog | — | Nearly black | Cold blue-grey ambient + warm accents, low exposure, no shadow noise | Shop windows glow pale, layered right |
| 10 | mercury_0HK4nBtDbI.jpg | 17:53 | Day 08:17, taxi drop-off, shops | Solid, peds solid | None | Grey-brown dust puffs over hood, translucent | Paving slabs + asphalt; grass tufts alpha at road edge | — | Pale hazy | Milky daylight, moderate contrast, slight teal in shadow | Dust reads over sky, over car |
| 11 | mercury_1tzVCYWeLx.png | 17:57 | Overcast midday 12:19, Pay'n'Spray garage | Solid; interior seats visible thru glass | None | Dark smoke plume visible against dark interior (correct contrast handling) | Large paving slabs, flat, no gloss | — | Off-screen / pale | Flat desaturated grey-green grade, soft contrast, no crushed blacks | Garage interior darkness preserved |
| 12 | mercury_RhYyQ7wuej.png | 18:00 | Day 14:45, combat street, fire + wreck | All solid; blue sedan metallic sheen on trunk/roof | None | Orange additive fire in wreck doorway, correct ordering | Pale grey-tan asphalt, faded yellow lines | — | Pale blue, low saturation | Bright neutral sun, mild contrast, soft bluish-grey shadows | Graffiti decals crisp, no z-fighting; yucca alpha correct |

Note: no water scenes in sample (none of the 12 frames show ocean/lakes — check wider set if water regression suspected).

## Target State Definition

**Overall grade.** Neutral-to-slightly-warm base with scene-dependent desaturation. Day = pale, milky, low-saturation neutrals (grey-tan asphalt, washed concrete). Dusk = pastel pink/orange sky bands, warm horizontal cloud striping. Night = cold blue-grey ambient base with warm sodium accents — never all-black, never all-orange.

**Contrast.** Moderate and soft. Shadows are bluish-grey, not crushed black (shot 6, 11, 12). Highlights (lamp crowns, sun, neon) bloom softly, no hard clipping. Night keeps readable detail in shadowed walls (shots 9, 11).

**Ambient character.** Days: flat, slightly hazy ambient with gentle vertical gradient on buildings and mild distance fog. Nights: lifted blue-grey ambient so silhouettes stay readable; warm pools around lamps only. Indoors/garage interiors go dark but preserve form (shot 11).

**Coronas.** Street lamps: large, translucent, warm-orange discs with soft halo and faint anisotropic streak/flare (shots 1, 7, 8, 9). Traffic lights: small saturated point coronas (green, red). Coronas are additive and translucent — never opaque dots or huge blown white blobs.

**Spec / reflection intensity.** Vehicles carry a broad, soft specular band on body panels — visible but not mirror: faint env sheen on bumper/trunk corners (shots 6, 12), bright streaks only at night under lamps (shot 8). No chrome-plastic look; no sun-clipped blown highlights (shot 10 roof gloss is soft ochre, not white).

**Particles.** Smoke = translucent grey (tyre/damage/dust), correctly alpha-blended over sky and road, darkens plausibly against dark backgrounds (shot 11). Fire/sparks = additive orange, layered over the car and ground, never behind the emitting surface (shot 12, 8).

**Roads / ground.** Textured asphalt with double-yellow centre lines; slight warm sheen under lamplight at night (not mirror-wet); paving/dirt textures crisp at distance (shots 2, 5, 10).

**Draw order / alpha.** Vegetation cards, fences, grass tufts, decals, graffiti: correct alpha-tested ordering, zero see-through, zero z-fighting on decals. Car glass shows interior where appropriate (shots 5, 11). Smoke renders over the road, under HUD.

## Regression Checklist (verify after fixes)

1. **No see-through geometry** — buildings, vehicles, peds render fully opaque at all angles (compare shots 1, 5, 12).
2. **Corona shape** — lamp coronas are translucent warm discs with streak flares; not opaque dots, not white blobs (shots 1, 7, 8).
3. **Night ambient** — shadowed walls readable, blue-grey cast, not crushed to black (shots 9, 11).
4. **Day grade** — pale neutral/desaturated; no over-saturation, no yellow-green wash, no milky white lift on whites (shots 10, 12).
5. **Shadow colour** — bluish-grey soft shadows, never pitch black (shots 6, 12).
6. **Vehicle specular** — broad soft bands; present at dusk/night, subtle by day; no mirror or plastic look (shots 4, 6, 8, 12).
7. **Smoke blending** — translucent, correct layering over road/sky; visible against dark interiors (shots 7, 10, 11).
8. **Fire/sparks additive** — orange additive blend layered over the emitter, not hidden behind it (shots 8, 12).
9. **Alpha-tested vegetation/fences/decals** — clean edges, correct draw order, no flicker, no z-fighting on graffiti (shots 3, 4, 6, 12).
10. **Distance haze** — mild fog fades far buildings at dusk/night; day haze is subtle, not grey blanket (shots 5, 9).
11. **Road lines & ground texture** — double-yellow lines visible; asphalt/dirt texture reads at mid distance; night sheen subtle (shots 1, 2, 5).
12. **Sky gradients** — dusk shows pink/orange banding; morning pale cyan-pink; night near-black with warm horizon traces (shots 2, 3, 9).
13. **Garage/interior darkness** — interiors stay dark but preserve shape contrast (shot 11).
14. **Colour filter** — matches the modern-filter behaviour implied by the grade above (see AGENTS.md: PBR must run COLORFILTER_MODERN).
