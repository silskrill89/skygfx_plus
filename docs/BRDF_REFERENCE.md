# SkyGFX Plus - BRDF Material Reference
# CryEngine PBR Standard + Substance Designer Values
# Glossiness workflow: glossiness = 1.0 - roughness
# All dielectric F0 from IOR: F0 = ((n-1)/(n+1))^2

## Reference Sources
- CryEngine 5 PBR Texture Guidelines
- Naty Hoffman SIGGRAPH 2013 "Physics and Math of Shading"
- Disney BRDF (Burley 2012)
- UE4 Material Reference (Epic Games)
- refractiveindex.info (IOR data)
- Substance Designer default presets
- **TOG2021: "Non-Parametric Sparse BRDF" (Gaussian basis fitting from MERL measured data)**
  - Uses 2-32 Gaussian lobes to fit measured BRDFs
  - MERL database: 100+ real-world materials (metal, paint, fabric, wood, etc.)
  - Key insight: real-world BRDFs are NOT parametric — measured data > parametric models
  - Applications: material editing, classification, upsampling, removal
- **SMAA / Morphological AA papers** (in E:\docs) — future AA improvements
- **COD DTAA paper** — temporal anti-aliasing reference
- **RenderWare docs** (V2.1) — RW pipeline reference

## Material Table (Glossiness Workflow)

### MAN-MADE SURFACES (buildings, infrastructure)
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Concrete rough       | 0.10       | 0.040       | 0.0       | 0.8     | Cast concrete, unfinished |
| Concrete smooth      | 0.35       | 0.042       | 0.0       | 0.9     | Polished concrete floor |
| Concrete painted     | 0.40       | 0.040       | 0.3       | 1.0     | Painted concrete wall |
| Brick common         | 0.15       | 0.044       | 0.0       | 0.7     | Red brick |
| Brick painted        | 0.30       | 0.040       | 0.2       | 0.8     | Painted brick |
| Stucco               | 0.10       | 0.040       | 0.0       | 0.6     | Rough stucco render |
| Plaster              | 0.25       | 0.040       | 0.0       | 0.7     | Interior plaster wall |
| Drywall              | 0.20       | 0.040       | 0.0       | 0.6     | Painted drywall |
| Tile ceramic         | 0.65       | 0.048       | 0.4       | 1.0     | Bathroom/kitchen tile |
| Tile porcelain       | 0.80       | 0.045       | 0.5       | 1.1     | High-end porcelain |
| Tile roof            | 0.20       | 0.042       | 0.1       | 0.8     | Clay roof tile |
| Marble polished      | 0.85       | 0.045       | 0.3       | 1.2     | Polished marble |
| Marble rough         | 0.35       | 0.042       | 0.0       | 0.9     | Unpolished marble |
| Granite polished     | 0.80       | 0.045       | 0.2       | 1.1     | Polished granite counter |
| Stone rough cut      | 0.15       | 0.040       | 0.0       | 0.8     | Rough stone wall |
| Stone smooth         | 0.40       | 0.042       | 0.0       | 0.9     | Smooth stone facade |
| Asphalt              | 0.10       | 0.040       | 0.0       | 0.5     | Road surface |
| Asphalt wet          | 0.50       | 0.040       | 0.0       | 0.7     | Wet road |
| Gravel               | 0.05       | 0.040       | 0.0       | 0.5     | Loose gravel |
| Sand                 | 0.10       | 0.040       | 0.0       | 0.6     | Sand surface |
| Dirt                 | 0.05       | 0.040       | 0.0       | 0.5     | Dry dirt |
| Dirt wet             | 0.50       | 0.040       | 0.0       | 0.7     | Wet dirt/mud |
| Paving stone         | 0.25       | 0.042       | 0.0       | 0.8     | Paved walkway |
| Concrete curb        | 0.20       | 0.040       | 0.0       | 0.7     | Curb/kerb |

### GLASS SURFACES
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Glass clear          | 0.95       | 0.040       | 0.0       | 2.0     | Clear window glass |
| Glass tinted         | 0.90       | 0.042       | 0.0       | 1.8     | Tinted window |
| Glass frosted        | 0.40       | 0.040       | 0.0       | 1.0     | Frosted/obscured glass |
| Glass dirty          | 0.60       | 0.040       | 0.0       | 1.2     | Dirty window |
| Glass broken         | 0.30       | 0.040       | 0.0       | 0.8     | Cracked/broken glass |
| Mirror               | 1.00       | 0.040       | 0.0       | 2.5     | Perfect mirror |
| Plastic clear        | 0.80       | 0.040       | 0.0       | 1.5     | Clear plastic |
| Plastic opaque       | 0.50       | 0.040       | 0.8       | 1.2     | Opaque plastic (ABS) |
| Fiberglass           | 0.30       | 0.040       | 0.3       | 0.9     | Fiberglass panel |

### METAL SURFACES (metalness = 1)
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Steel                | 0.65       | 0.560       | 0.0       | 1.5     | Brushed steel |
| Steel polished       | 0.90       | 0.560       | 0.0       | 2.0     | Polished chrome steel |
| Iron                 | 0.50       | 0.560       | 0.0       | 1.5     | Iron surface |
| Iron rusted          | 0.15       | 0.560       | 0.0       | 1.0     | Rusted iron |
| Aluminum             | 0.70       | 0.910       | 0.0       | 1.5     | Brushed aluminum |
| Chrome               | 0.95       | 0.950       | 0.0       | 2.0     | Chrome plating |
| Copper               | 0.60       | 0.950       | 0.0       | 1.5     | Copper surface |
| Brass                | 0.55       | 0.920       | 0.0       | 1.4     | Brass surface |
| Gold                 | 0.80       | 0.980       | 0.0       | 1.8     | Gold surface |
| Silver               | 0.90       | 0.970       | 0.0       | 2.0     | Silver surface |
| Tin                  | 0.70       | 0.850       | 0.0       | 1.4     | Tin can |
| Zinc                 | 0.60       | 0.830       | 0.0       | 1.3     | Galvanized zinc |
| Metal painted        | 0.60       | 0.040       | 0.5       | 1.0     | Painted metal (dielectric) |
| Metal corrugated     | 0.30       | 0.560       | 0.0       | 1.2     | Corrugated sheet metal |
| Metal mesh           | 0.40       | 0.560       | 0.0       | 1.0     | Chain link / mesh |

### WOOD SURFACES
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Wood painted         | 0.50       | 0.040       | 0.3       | 1.0     | Painted wood |
| Wood varnished       | 0.70       | 0.040       | 0.8       | 1.2     | Varnished/polished wood |
| Wood bare            | 0.25       | 0.038       | 0.0       | 0.7     | Unfinished wood |
| Wood aged            | 0.10       | 0.038       | 0.0       | 0.6     | Weathered old wood |
| Wood stained         | 0.40       | 0.040       | 0.2       | 0.9     | Stained wood |
| Plywood              | 0.20       | 0.040       | 0.0       | 0.7     | Plywood panel |
| MDF                  | 0.15       | 0.040       | 0.0       | 0.6     | MDF board |
| Laminate             | 0.60       | 0.040       | 0.5       | 1.0     | Laminate flooring |
| Decking              | 0.15       | 0.040       | 0.0       | 0.7     | Outdoor wood decking |
| Fence                | 0.10       | 0.038       | 0.0       | 0.6     | Wooden fence |
| Door                 | 0.40       | 0.040       | 0.3       | 1.0     | Painted/varnished door |
| Shutters             | 0.30       | 0.040       | 0.2       | 0.8     | Window shutters |

### ROOFING
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Roof tar             | 0.05       | 0.040       | 0.0       | 0.4     | Tar paper roof |
| Roof gravel          | 0.05       | 0.040       | 0.0       | 0.4     | Gravel built-up roof |
| Roof metal           | 0.40       | 0.560       | 0.0       | 1.2     | Metal roof |
| Roof shingle         | 0.10       | 0.040       | 0.0       | 0.5     | Asphalt shingle |
| Roof slate           | 0.20       | 0.040       | 0.0       | 0.7     | Slate roof |

### PAINT / COATING
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Paint gloss          | 0.80       | 0.040       | 0.8       | 1.2     | High-gloss paint |
| Paint semi-gloss     | 0.50       | 0.040       | 0.4       | 1.0     | Semi-gloss paint |
| Paint matte          | 0.20       | 0.040       | 0.0       | 0.8     | Flat/matte paint |
| Paint eggshell       | 0.35       | 0.040       | 0.2       | 0.9     | Eggshell finish |
| Spray paint          | 0.30       | 0.040       | 0.1       | 0.9     | Graffiti / spray paint |
| Automotive paint     | 0.85       | 0.040       | 0.9       | 1.3     | Car paint (clearcoat) |
| Primer               | 0.15       | 0.040       | 0.0       | 0.7     | Paint primer |

### ORGANIC / NATURAL
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Grass                | 0.10       | 0.040       | 0.0       | 0.6     | Grass |
| Grass long           | 0.10       | 0.040       | 0.0       | 0.5     | Tall grass |
| Leaves               | 0.20       | 0.040       | 0.0       | 0.5     | Tree leaves |
| Bark                 | 0.05       | 0.040       | 0.0       | 0.4     | Tree bark |
| Flower               | 0.15       | 0.040       | 0.0       | 0.5     | Flower petals |
| Cactus               | 0.15       | 0.040       | 0.0       | 0.5     | Cactus surface |
| Bush                 | 0.10       | 0.040       | 0.0       | 0.5     | Bush/shrub |
| Hay                  | 0.05       | 0.040       | 0.0       | 0.4     | Hay/straw |

### SKIN / CLOTH
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Skin                 | 0.40       | 0.040       | 0.0       | 0.6     | Human skin |
| Leather              | 0.50       | 0.040       | 0.3       | 0.8     | Leather surface |
| Cloth                | 0.10       | 0.040       | 0.0       | 0.4     | Fabric |
| Carpet               | 0.05       | 0.040       | 0.0       | 0.3     | Carpet |
| Denim                | 0.10       | 0.040       | 0.0       | 0.4     | Denim fabric |
| Silk                 | 0.60       | 0.040       | 0.0       | 0.7     | Silk fabric |

### SPECIAL
| Surface              | Glossiness | F0 (linear) | Clearcoat | SpecInt | Notes |
|----------------------|------------|-------------|-----------|---------|-------|
| Water                | 0.98       | 0.020       | 0.0       | 1.0     | Still water |
| Ice                  | 0.90       | 0.020       | 0.0       | 1.0     | Ice surface |
| Rubber               | 0.10       | 0.040       | 0.0       | 0.4     | Rubber tire |
| Tire                 | 0.10       | 0.040       | 0.0       | 0.4     | Car tire |
| Concrete barrier     | 0.15       | 0.040       | 0.0       | 0.7     | Road barrier |
| Guardrail            | 0.40       | 0.560       | 0.0       | 1.0     | Metal guardrail |
| Sign                 | 0.60       | 0.040       | 0.3       | 1.0     | Road sign (reflective) |
| Billboard            | 0.30       | 0.040       | 0.0       | 0.8     | Billboard |
| Neon                 | 0.80       | 0.040       | 0.0       | 1.5     | Neon tube (emissive) |
| Lamp                 | 0.90       | 0.040       | 0.0       | 2.0     | Lamp glass |
| Cable                | 0.30       | 0.560       | 0.0       | 1.0     | Metal cable |
| Pipe                 | 0.50       | 0.560       | 0.0       | 1.2     | Metal pipe |
| Pipe PVC             | 0.50       | 0.040       | 0.8       | 1.0     | PVC pipe |
| Vent                 | 0.40       | 0.560       | 0.0       | 1.0     | Metal vent |
| AC unit              | 0.40       | 0.560       | 0.0       | 1.0     | AC unit metal |
| Dumpster             | 0.30       | 0.560       | 0.0       | 1.0     | Metal dumpster |
| Trash can            | 0.30       | 0.560       | 0.0       | 1.0     | Metal trash can |
| Fire hydrant         | 0.40       | 0.560       | 0.0       | 1.2     | Metal hydrant |
| Bench                | 0.30       | 0.040       | 0.0       | 0.7     | Park bench |
| Fence metal          | 0.40       | 0.560       | 0.0       | 1.0     | Metal fence |
| Fence chain          | 0.30       | 0.560       | 0.0       | 1.0     | Chain link fence |
| Manhole              | 0.20       | 0.560       | 0.0       | 1.0     | Manhole cover |
| Sidewalk             | 0.15       | 0.040       | 0.0       | 0.7     | Concrete sidewalk |
| Curb                 | 0.20       | 0.040       | 0.0       | 0.7     | Concrete curb |

## Shader Constants Layout
c22 = {glossiness, reflectance, clearcoat, subsurface}
c23 = {specularInt, metalness, 0, 0}

## GTA SA Texture → BRDF Mapping (pattern-based)
- "glass", "window", "wndw" → Glass clear/tinted
- "concrete", "conc" → Concrete rough/smooth
- "brick", "brk" → Brick
- "metal", "mtl", "steel" → Metal
- "wood", "wd", "plank" → Wood
- "tile" → Tile
- "asphalt", "road", "pave" → Asphalt
- "marble" → Marble
- "stone", "stn" → Stone
- "paint", "pnt" → Paint
- "roof" → Roofing
- "grass", "grs" → Grass
- "dirt", "sand" → Dirt/Sand
- "rubber", "tire" → Rubber
- "plastic" → Plastic
- "sign" → Sign
- "neon", "lamp", "light" → Lamp/Neon
- "barrier", "guardrail" → Barrier
- "pipe", "vent", "cable" → Metal infrastructure

## Approach: 2D Lighting → 3D Propagation (Radiance Cascades)
Source: "Exploring a New Approach to Realistic Lighting: Radiance Cascades"
- Compute lighting in 2D screen-space first
- Use radiance cascades for efficient 2D global illumination
- Propagate 2D lighting to 3D via vertex shading
- Use vertex color as carrier for 2D-computed lighting
- Trick: multiply vertex light by PBR BRDF for plausible 3D result

## Approach: Water (Path of Exile 2 style)
Source: "ExileCon 2023 - Rendering Path of Exile 2"
- Screen-space reflections via ray marching in depth buffer
- Gerstner wave displacement for surface geometry
- Layered normals (large waves + detail noise)
- Fresnel-based blend between reflection and refraction
- Caustics via projected noise pattern
- Foam via depth-based edge detection

## Obsidian Vault
Full interactive reference: `docs/obsidian-vault/` (open in Obsidian)
- [[Non-Parametric Sparse BRDF]] — measured BRDF data from MERL
- [[SkyGFX Pipeline Overview]] — rendering pipeline details
- [[GTA San Andreas EPK Reference]] — visual target screenshots
