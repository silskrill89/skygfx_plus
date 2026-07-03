# SkyGFX Plus — Living Systems Database & Feature Roadmap

## Vision Statement

SkyGFX Plus is not a static mod — it's a **living graphics engine** that grows year-over-year by:
- **Connecting existing game systems** that were previously isolated
- **Restoring cut/unused features** from GTA SA's development
- **Enhancing visual fidelity** through efficient shader improvements
- **Avoiding scope creep** by building on what already exists
- **Performance-first design** — every feature must maintain 60+ FPS on mid-range hardware

The goal: Make GTA SA feel like it continued evolving as if Rockstar kept developing it, with each year adding depth to existing systems rather than bolting on unrelated features.

---

## Part 1: Vehicle Database — Real-World Analogs

### Methodology
Each GTA SA vehicle is mapped to real-world counterparts based on:
- **Design language** (body style, proportions, era-specific styling)
- **Mechanical layout** (FR/FF/MR, engine type, suspension)
- **Market segment** (economy, luxury, performance, utility)
- **Cultural context** (American muscle, Japanese tuner, European luxury, etc.)

This database enables:
- **Accurate wear patterns** (older cars rust differently than newer ones)
- **Realistic material properties** (80s plastics vs 90s composites)
- **Model year variations** (facelifts, trim levels, regional differences)
- **Damage modeling** (crumple zones, glass breakage patterns)

---

### Compact Cars

#### **Admiral** (Model ID: 445)
- **Real-world analogs**: 1988-1992 Honda Accord, 1989-1994 Nissan Maxima, 1990-1995 Toyota Camry
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s Japanese sedan)
- **Wear characteristics**:
  - Rust: Wheel arches, rocker panels, rear quarter panels (Japanese steel corrosion)
  - Paint fade: Roof, hood (single-stage paint common in 80s Japanese cars)
  - Interior: Cracked dashboard, faded cloth seats
- **Common damage**: Front bumper sag, headlight yellowing, exhaust rust-through
- **Material notes**: Thin sheet metal (0.7mm), plastic bumpers, minimal sound deadening

#### **Blista Compact** (Model ID: 492)
- **Real-world analogs**: 1983-1987 Honda CRX, 1985-1989 Toyota Corolla AE86, 1986-1991 Honda Civic
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s Japanese hatchback)
- **Wear characteristics**:
  - Rust: Rear hatch, floor pans, strut towers
  - Paint: Chalking on horizontal surfaces, clear coat failure on roof
  - Interior: Cracked steering wheel, worn shift knob
- **Common damage**: Rear hatch misalignment, cracked tail lights, rusted exhaust hangers
- **Material notes**: Lightweight construction (900kg), minimal rust protection, single-stage white/black paint common

#### **Bravura** (Model ID: 401)
- **Real-world analogs**: 1982-1987 Chevrolet Cavalier, 1984-1988 Ford Tempo, 1985-1989 Plymouth Reliant
- **Model year**: 1984-1988
- **Era**: VERA_80S (mid-80s American compact)
- **Wear characteristics**:
  - Rust: Quarter panels, trunk lid, door bottoms (American steel quality issues)
  - Paint: Orange peel texture, clear coat peeling on hood/roof
  - Interior: Cracked vinyl seats, broken window regulators
- **Common damage**: Bumper cover separation, cracked windshield trim, rusted subframe
- **Material notes**: Heavy gauge steel (1.0mm), poor rust-proofing, vinyl interiors

#### **Elegant** (Model ID: 507)
- **Real-world analogs**: 1990-1994 Lexus ES250, 1991-1995 Acura Legend, 1992-1996 Infiniti J30
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s Japanese luxury)
- **Wear characteristics**:
  - Rust: Minimal (galvanized steel), rear wheel arches in salt-belt regions
  - Paint: Excellent durability, minor swirl marks, headlight oxidation
  - Interior: Leather cracking on bolsters, wood trim fading
- **Common damage**: Power window motor failure, sunroof leaks, suspension bushing wear
- **Material notes**: Galvanized steel (1.2mm), multi-stage paint, leather/vinyl mix interior

#### **Esperanto** (Model ID: 419)
- **Real-world analogs**: 1988-1992 Ford Taurus, 1989-1993 Pontiac Grand Prix, 1990-1994 Chevrolet Lumina
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s American mid-size)
- **Wear characteristics**:
  - Rust: Rocker panels, door bottoms, rear wheel wells
  - Paint: Fading on dark colors, clear coat failure on trunk
  - Interior: Dashboard cracking, headliner sagging
- **Common damage**: Bumper cover warping, grille cracking, exhaust manifold cracks
- **Material notes**: Moderate steel thickness (0.9mm), plastic grille/bumper covers, cloth/vinyl seats

#### **Greenwood** (Model ID: 492)
- **Real-world analogs**: 1978-1982 Chevrolet Malibu, 1979-1983 Ford Fairmont, 1980-1984 Plymouth Volare
- **Model year**: 1979-1983
- **Era**: VERA_PRE80 (late 70s/early 80s American sedan)
- **Wear characteristics**:
  - Rust: Severe — fenders, quarter panels, trunk floor, door bottoms
  - Paint: Chalking, fading, single-stage paint failure
  - Interior: Vinyl cracking, foam deterioration, broken seat tracks
- **Common damage**: Bumper rust, grille breakage, suspension component fatigue
- **Material notes**: Heavy steel (1.2mm), minimal rust protection, vinyl bench seats

#### **Intruder** (Model ID: 546)
- **Real-world analogs**: 1988-1992 Honda Accord, 1989-1993 Nissan Stanza, 1990-1994 Mazda 626
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s Japanese sedan)
- **Wear characteristics**:
  - Rust: Rear wheel arches, rocker panels, trunk lid
  - Paint: Clear coat failure on hood, fading on roof
  - Interior: Dashboard cracking, worn steering wheel
- **Common damage**: Headlight yellowing, bumper sag, exhaust rust
- **Material notes**: Thin steel (0.8mm), plastic bumpers, cloth interior

#### **Manana** (Model ID: 410)
- **Real-world analogs**: 1975-1979 Chevrolet Nova, 1976-1980 Ford Granada, 1977-1981 Plymouth Fury
- **Model year**: 1976-1980
- **Era**: VERA_PRE80 (mid-70s American compact)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Heavy steel (1.3mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Premier** (Model ID: 426)
- **Real-world analogs**: 1992-1996 Toyota Camry, 1993-1997 Honda Accord, 1994-1998 Nissan Altima
- **Model year**: 1993-1997
- **Era**: VERA_90S (mid-90s Japanese sedan)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels (better than 80s)
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear on seats, dashboard fading
- **Common damage**: Power window failure, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, cloth/vinyl interior

#### **Previon** (Model ID: 436)
- **Real-world analogs**: 1985-1989 Toyota Celica, 1986-1990 Honda Prelude, 1987-1991 Nissan 200SX
- **Model year**: 1986-1990
- **Era**: VERA_80S (mid-80s Japanese coupe)
- **Wear characteristics**:
  - Rust: Rear hatch, floor pans, strut towers
  - Paint: Clear coat failure on hood, fading on roof
  - Interior: Dashboard cracking, worn shift knob
- **Common damage**: Headlight yellowing, rear hatch misalignment, exhaust rust
- **Material notes**: Lightweight steel (0.8mm), plastic bumpers, cloth/vinyl interior

#### **Sunrise** (Model ID: 550)
- **Real-world analogs**: 1995-1999 Nissan Maxima, 1996-2000 Toyota Avalon, 1997-2001 Honda Accord
- **Model year**: 1996-2000
- **Era**: VERA_90S (late 90s Japanese sedan)
- **Wear characteristics**:
  - Rust: Minimal — rear wheel arches in salt regions
  - Paint: Excellent durability, minor swirl marks
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Power window motor failure, suspension wear, exhaust rust
- **Material notes**: Galvanized steel (1.1mm), multi-stage paint, leather/cloth interior

#### **Tahoma** (Model ID: 566)
- **Real-world analogs**: 1991-1996 Chevrolet Impala SS, 1992-1997 Ford Crown Victoria, 1993-1998 Pontiac Bonneville
- **Model year**: 1992-1996
- **Era**: VERA_90S (early 90s American full-size)
- **Wear characteristics**:
  - Rust: Rocker panels, door bottoms, trunk lid
  - Paint: Clear coat failure on hood/roof, fading on dark colors
  - Interior: Cloth wear, dashboard cracking, headliner sag
- **Common damage**: Bumper cover warping, grille cracking, suspension bushing wear
- **Material notes**: Heavy steel (1.2mm), plastic grille/bumper covers, cloth/vinyl interior

#### **Vincent** (Model ID: 540)
- **Real-world analogs**: 1988-1992 Honda Civic, 1989-1993 Toyota Corolla, 1990-1994 Nissan Sentra
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s Japanese compact)
- **Wear characteristics**:
  - Rust: Rear wheel arches, rocker panels, trunk lid
  - Paint: Clear coat failure on hood, fading on roof
  - Interior: Dashboard cracking, worn steering wheel
- **Common damage**: Headlight yellowing, bumper sag, exhaust rust
- **Material notes**: Thin steel (0.8mm), plastic bumpers, cloth interior

#### **Washington** (Model ID: 421)
- **Real-world analogs**: 1990-1994 Chevrolet Caprice, 1991-1995 Ford Crown Victoria, 1992-1996 Pontiac Bonneville
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s American full-size)
- **Wear characteristics**:
  - Rust: Rocker panels, door bottoms, trunk lid
  - Paint: Clear coat failure on hood/roof, fading on dark colors
  - Interior: Cloth wear, dashboard cracking, headliner sag
- **Common damage**: Bumper cover warping, grille cracking, suspension bushing wear
- **Material notes**: Heavy steel (1.2mm), plastic grille/bumper covers, cloth/vinyl interior

#### **Willard** (Model ID: 529)
- **Real-world analogs**: 1986-1990 Ford Taurus, 1987-1991 Pontiac 6000, 1988-1992 Chevrolet Celebrity
- **Model year**: 1987-1991
- **Era**: VERA_80S (late 80s American mid-size)
- **Wear characteristics**:
  - Rust: Rocker panels, door bottoms, rear wheel wells
  - Paint: Fading on dark colors, clear coat failure on trunk
  - Interior: Dashboard cracking, headliner sagging
- **Common damage**: Bumper cover warping, grille cracking, exhaust manifold cracks
- **Material notes**: Moderate steel thickness (0.9mm), plastic grille/bumper covers, cloth/vinyl seats

---

### Muscle Cars

#### **Blade** (Model ID: 536)
- **Real-world analogs**: 1964-1967 Chevrolet Impala, 1965-1968 Ford Galaxie, 1966-1969 Plymouth Fury
- **Model year**: 1965-1968
- **Era**: VERA_PRE80 (mid-60s American full-size)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk, roof
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Heavy steel (1.4mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Broadway** (Model ID: 575)
- **Real-world analogs**: 1970-1973 Cadillac Eldorado, 1971-1974 Lincoln Continental, 1972-1975 Chrysler Imperial
- **Model year**: 1971-1974
- **Era**: VERA_PRE80 (early 70s American luxury)
- **Wear characteristics**:
  - Rust: Severe — fenders, quarter panels, trunk, roof, floor pans
  - Paint: Fading, chalking, clear coat failure (if equipped)
  - Interior: Leather cracking, wood trim fading, vinyl deterioration
- **Common damage**: Bumper rust, grille breakage, suspension component fatigue
- **Material notes**: Heavy steel (1.5mm), poor rust-proofing, leather/vinyl interior, wood trim

#### **Buccaneer** (Model ID: 518)
- **Real-world analogs**: 1968-1972 Chevrolet Chevelle, 1969-1973 Pontiac GTO, 1970-1974 Plymouth Road Runner
- **Model year**: 1969-1972
- **Era**: VERA_PRE80 (late 60s/early 70s American muscle)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk, hood
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Heavy steel (1.3mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Clover** (Model ID: 542)
- **Real-world analogs**: 1978-1982 Chevrolet Camaro, 1979-1983 Pontiac Firebird, 1980-1984 Ford Mustang
- **Model year**: 1979-1982
- **Era**: VERA_PRE80 (late 70s/early 80s American pony car)
- **Wear characteristics**:
  - Rust: Severe — fenders, quarter panels, floor pans, trunk, T-top seams
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking, broken window regulators
- **Common damage**: Bumper rust, grille breakage, suspension component fatigue
- **Material notes**: Moderate steel (1.1mm), poor rust-proofing, vinyl interior, T-top leaks common

#### **Feltzer** (Model ID: 533)
- **Real-world analogs**: 1990-1994 Nissan 300ZX, 1991-1995 Mazda RX-7, 1992-1996 Toyota Supra
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s Japanese sports car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, leather/cloth interior

#### **Hermes** (Model ID: 489)
- **Real-world analogs**: 1968-1972 Chevrolet Chevelle, 1969-1973 Pontiac GTO, 1970-1974 Oldsmobile Cutlass
- **Model year**: 1969-1972
- **Era**: VERA_PRE80 (late 60s/early 70s American muscle)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk, hood
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Heavy steel (1.3mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Phoenix** (Model ID: 603)
- **Real-world analogs**: 1978-1982 Pontiac Firebird Trans Am, 1979-1983 Chevrolet Camaro Z28, 1980-1984 Ford Mustang GT
- **Model year**: 1979-1982
- **Era**: VERA_PRE80 (late 70s/early 80s American pony car)
- **Wear characteristics**:
  - Rust: Severe — fenders, quarter panels, floor pans, trunk, T-top seams
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking, broken window regulators
- **Common damage**: Bumper rust, grille breakage, suspension component fatigue
- **Material notes**: Moderate steel (1.1mm), poor rust-proofing, vinyl interior, T-top leaks common

#### **Sabre** (Model ID: 431)
- **Real-world analogs**: 1968-1972 Chevrolet Chevelle SS, 1969-1973 Pontiac GTO Judge, 1970-1974 Plymouth 'Cuda
- **Model year**: 1969-1972
- **Era**: VERA_PRE80 (late 60s/early 70s American muscle)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk, hood
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Heavy steel (1.3mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Stallion** (Model ID: 439)
- **Real-world analogs**: 1967-1970 Ford Mustang, 1968-1971 Chevrolet Camaro, 1969-1972 Pontiac Firebird
- **Model year**: 1968-1971
- **Era**: VERA_PRE80 (late 60s/early 70s American pony car)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk, hood
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Heavy steel (1.2mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Tampa** (Model ID: 549)
- **Real-world analogs**: 1968-1972 Chevrolet Nova, 1969-1973 Ford Maverick, 1970-1974 Plymouth Duster
- **Model year**: 1969-1972
- **Era**: VERA_PRE80 (late 60s/early 70s American compact muscle)
- **Wear characteristics**:
  - Rust: Extreme — fenders, quarter panels, floor pans, trunk, hood
  - Paint: Severe fading, chalking, single-stage paint failure
  - Interior: Vinyl cracking, foam disintegration, broken door handles
- **Common damage**: Bumper rust-through, grille breakage, suspension collapse
- **Material notes**: Moderate steel (1.1mm), poor rust-proofing, vinyl interior, minimal safety features

#### **Virgo** (Model ID: 491)
- **Real-world analogs**: 1988-1992 Ford Thunderbird, 1989-1993 Pontiac Grand Prix, 1990-1994 Chevrolet Monte Carlo
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s/early 90s American coupe)
- **Wear characteristics**:
  - Rust: Moderate — rocker panels, door bottoms, rear wheel wells
  - Paint: Fading on dark colors, clear coat failure on trunk
  - Interior: Dashboard cracking, headliner sagging
- **Common damage**: Bumper cover warping, grille cracking, exhaust manifold cracks
- **Material notes**: Moderate steel thickness (0.9mm), plastic grille/bumper covers, cloth/vinyl seats

#### **Voodoo** (Model ID: 412)
- **Real-world analogs**: 1970-1973 Chevrolet Impala, 1971-1974 Pontiac Catalina, 1972-1975 Oldsmobile Delta 88
- **Model year**: 1971-1974
- **Era**: VERA_PRE80 (early 70s American full-size)
- **Wear characteristics**:
  - Rust: Severe — fenders, quarter panels, trunk, roof, floor pans
  - Paint: Fading, chalking, clear coat failure (if equipped)
  - Interior: Vinyl cracking, foam deterioration, broken door handles
- **Common damage**: Bumper rust, grille breakage, suspension component fatigue
- **Material notes**: Heavy steel (1.4mm), poor rust-proofing, vinyl interior, minimal safety features

---

### Sports Cars

#### **Alpha** (Model ID: 602)
- **Real-world analogs**: 1990-1994 Mazda MX-5 Miata, 1991-1995 Toyota MR2, 1992-1996 Honda CRX del Sol
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s Japanese roadster)
- **Wear characteristics**:
  - Rust: Moderate — rocker panels, rear wheel arches, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear on seats, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Lightweight steel (0.8mm), multi-stage paint, cloth/vinyl interior

#### **Banshee** (Model ID: 429)
- **Real-world analogs**: 1984-1989 Chevrolet Corvette C4, 1985-1990 Pontiac Fiero, 1986-1991 Toyota Supra
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s American/Japanese sports car)
- **Wear characteristics**:
  - Rust: Moderate — rocker panels, rear wheel arches, trunk
  - Paint: Clear coat failure on hood, fading on roof
  - Interior: Dashboard cracking, leather wear on bolsters
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Fiberglass/plastic body panels, steel frame, leather/vinyl interior

#### **Bullet** (Model ID: 541)
- **Real-world analogs**: 1990-1994 Acura NSX, 1991-1995 Honda NSX, 1992-1996 Ferrari 348
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s exotic sports car)
- **Wear characteristics**:
  - Rust: Minimal — aluminum body panels, galvanized steel frame
  - Paint: Excellent durability, minor swirl marks
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Aluminum body panels, galvanized steel frame, leather interior

#### **Cheetah** (Model ID: 415)
- **Real-world analogs**: 1984-1989 Ferrari Testarossa, 1985-1990 Lamborghini Countach, 1986-1991 Porsche 959
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s exotic supercar)
- **Wear characteristics**:
  - Rust: Minimal — aluminum/fiberglass body panels, steel frame
  - Paint: Excellent durability, minor swirl marks
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Aluminum/fiberglass body panels, steel frame, leather interior

#### **Comet** (Model ID: 480)
- **Real-world analogs**: 1987-1991 Porsche 944, 1988-1992 BMW 325i, 1989-1993 Mercedes-Benz 190E
- **Model year**: 1988-1992
- **Era**: VERA_80S (late 80s European sports coupe)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, leather interior

#### **Elegy** (Model ID: 562)
- **Real-world analogs**: 1989-1994 Nissan Skyline GT-R R32, 1990-1995 Toyota Supra, 1991-1996 Mazda RX-7
- **Model year**: 1990-1994
- **Era**: VERA_90S (early 90s Japanese sports car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, leather/cloth interior

#### **Euros** (Model ID: 587)
- **Real-world analogs**: 1995-1999 BMW M3 E36, 1996-2000 Audi S4, 1997-2001 Mercedes-Benz C36 AMG
- **Model year**: 1996-2000
- **Era**: VERA_90S (late 90s European sports sedan)
- **Wear characteristics**:
  - Rust: Minimal — galvanized steel, excellent rust-proofing
  - Paint: Excellent durability, minor swirl marks
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.1mm), multi-stage paint, leather interior

#### **Flash** (Model ID: 565)
- **Real-world analogs**: 1995-1999 Honda Civic Si, 1996-2000 Toyota Corolla GT-S, 1997-2001 Nissan Sentra SE-R
- **Model year**: 1996-2000
- **Era**: VERA_90S (late 90s Japanese sport compact)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear on seats, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (0.9mm), multi-stage paint, cloth/vinyl interior

#### **Jester** (Model ID: 559)
- **Real-world analogs**: 1990-1994 Toyota Supra, 1991-1995 Mazda RX-7, 1992-1996 Nissan 300ZX
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s Japanese sports car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, leather/cloth interior

#### **Stratum** (Model ID: 561)
- **Real-world analogs**: 1992-1996 Subaru Impreza WRX, 1993-1997 Mitsubishi Lancer Evolution, 1994-1998 Toyota Celica GT-Four
- **Model year**: 1993-1997
- **Era**: VERA_90S (mid-90s Japanese rally car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear on seats, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, cloth/vinyl interior

#### **Sultan** (Model ID: 560)
- **Real-world analogs**: 1992-1996 Mitsubishi Lancer Evolution, 1993-1997 Subaru Impreza WRX, 1994-1998 Toyota Celica GT-Four
- **Model year**: 1993-1997
- **Era**: VERA_90S (mid-90s Japanese rally car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear on seats, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, cloth/vinyl interior

#### **Super GT** (Model ID: 502)
- **Real-world analogs**: 1995-1999 Nissan Skyline GT-R R33, 1996-2000 Toyota Supra, 1997-2001 Mazda RX-7
- **Model year**: 1996-2000
- **Era**: VERA_90S (late 90s Japanese sports car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, leather/cloth interior

#### **Turismo** (Model ID: 451)
- **Real-world analogs**: 1987-1991 Ferrari F40, 1988-1992 Lamborghini Diablo, 1989-1993 Porsche 959
- **Model year**: 1988-1992
- **Era**: VERA_80S (late 80s exotic supercar)
- **Wear characteristics**:
  - Rust: Minimal — aluminum/fiberglass body panels, steel frame
  - Paint: Excellent durability, minor swirl marks
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Aluminum/fiberglass body panels, steel frame, leather interior

#### **Uranus** (Model ID: 558)
- **Real-world analogs**: 1992-1996 Honda Prelude, 1993-1997 Toyota Celica, 1994-1998 Nissan 240SX
- **Model year**: 1993-1997
- **Era**: VERA_90S (mid-90s Japanese sports coupe)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear on seats, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (0.9mm), multi-stage paint, cloth/vinyl interior

#### **Windsor** (Model ID: 555)
- **Real-world analogs**: 1995-1999 Mercedes-Benz SL500, 1996-2000 BMW Z3, 1997-2001 Porsche Boxster
- **Model year**: 1996-2000
- **Era**: VERA_90S (late 90s European roadster)
- **Wear characteristics**:
  - Rust: Minimal — galvanized steel, excellent rust-proofing
  - Paint: Excellent durability, minor swirl marks
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.1mm), multi-stage paint, leather interior

#### **ZR-350** (Model ID: 477)
- **Real-world analogs**: 1990-1994 Mazda RX-7, 1991-1995 Toyota Supra, 1992-1996 Nissan 300ZX
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s Japanese sports car)
- **Wear characteristics**:
  - Rust: Moderate — rear wheel arches, rocker panels, trunk
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Leather wear on bolsters, dashboard fading
- **Common damage**: Headlight yellowing, suspension bushing wear, exhaust rust
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, leather/cloth interior

---

### SUVs & Trucks

#### **Bobcat** (Model ID: 422)
- **Real-world analogs**: 1984-1988 Ford Ranger, 1985-1989 Chevrolet S-10, 1986-1990 Dodge Dakota
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s American compact pickup)
- **Wear characteristics**:
  - Rust: Severe — bed, fenders, rocker panels, tailgate
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Tailgate rust, bed liner wear, suspension sag
- **Material notes**: Heavy steel (1.2mm), poor rust-proofing, vinyl interior

#### **Burrito** (Model ID: 482)
- **Real-world analogs**: 1985-1989 Chevrolet Astro, 1986-1990 Ford Aerostar, 1987-1991 Dodge Caravan
- **Model year**: 1986-1990
- **Era**: VERA_80S (mid-80s American minivan)
- **Wear characteristics**:
  - Rust: Moderate — rocker panels, rear wheel arches, tailgate
  - Paint: Fading on dark colors, clear coat failure on hood
  - Interior: Cloth wear, dashboard cracking, headliner sag
- **Common damage**: Sliding door mechanism failure, suspension bushing wear, exhaust rust
- **Material notes**: Moderate steel (0.9mm), plastic bumpers, cloth/vinyl interior

#### **Cavalcade** (Model ID: 400)
- **Real-world analogs**: 1992-1996 Ford Explorer, 1993-1997 Chevrolet Blazer, 1994-1998 Jeep Grand Cherokee
- **Model year**: 1993-1997
- **Era**: VERA_90S (mid-90s American SUV)
- **Wear characteristics**:
  - Rust: Moderate — rocker panels, rear wheel arches, tailgate
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear, dashboard fading
- **Common damage**: Suspension bushing wear, exhaust rust, power window failure
- **Material notes**: Galvanized steel (1.0mm), multi-stage paint, cloth/vinyl interior

#### **Flatbed** (Model ID: 455)
- **Real-world analogs**: 1988-1992 Ford F-250, 1989-1993 Chevrolet C/K 2500, 1990-1994 Dodge Ram 2500
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s American heavy truck)
- **Wear characteristics**:
  - Rust: Severe — bed, fenders, rocker panels, tailgate, cab corners
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Tailgate rust, bed liner wear, suspension sag
- **Material notes**: Heavy steel (1.4mm), poor rust-proofing, vinyl interior

#### **Mesa** (Model ID: 500)
- **Real-world analogs**: 1984-1988 Jeep Cherokee XJ, 1985-1989 Ford Bronco II, 1986-1990 Chevrolet S-10 Blazer
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s American compact SUV)
- **Wear characteristics**:
  - Rust: Severe — rocker panels, rear wheel arches, tailgate, floor pans
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Suspension bushing wear, exhaust rust, power window failure
- **Material notes**: Heavy steel (1.1mm), poor rust-proofing, vinyl interior

#### **Patriot** (Model ID: 470)
- **Real-world analogs**: 1984-1988 AM General HMMWV, 1985-1989 Chevrolet K5 Blazer, 1986-1990 Dodge Ramcharger
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s American military/full-size SUV)
- **Wear characteristics**:
  - Rust: Severe — body panels, frame, tailgate
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Suspension component fatigue, exhaust rust, power window failure
- **Material notes**: Heavy steel (1.3mm), poor rust-proofing, vinyl interior

#### **Perennial** (Model ID: 404)
- **Real-world analogs**: 1990-1994 Ford Aerostar, 1991-1995 Chevrolet Astro, 1992-1996 Dodge Caravan
- **Model year**: 1991-1995
- **Era**: VERA_90S (early 90s American minivan)
- **Wear characteristics**:
  - Rust: Moderate — rocker panels, rear wheel arches, tailgate
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear, dashboard fading
- **Common damage**: Sliding door mechanism failure, suspension bushing wear, exhaust rust
- **Material notes**: Moderate steel (0.9mm), plastic bumpers, cloth/vinyl interior

#### **Rancher** (Model ID: 489)
- **Real-world analogs**: 1988-1992 Ford F-150, 1989-1993 Chevrolet C/K 1500, 1990-1994 Dodge Ram 1500
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s American full-size pickup)
- **Wear characteristics**:
  - Rust: Severe — bed, fenders, rocker panels, tailgate, cab corners
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Tailgate rust, bed liner wear, suspension sag
- **Material notes**: Heavy steel (1.2mm), poor rust-proofing, vinyl interior

#### **Regina** (Model ID: 479)
- **Real-world analogs**: 1985-1989 Chevrolet Caprice wagon, 1986-1990 Ford LTD Country Squire, 1987-1991 Plymouth Gran Fury wagon
- **Model year**: 1986-1990
- **Era**: VERA_80S (mid-80s American station wagon)
- **Wear characteristics**:
  - Rust: Severe — rocker panels, rear wheel arches, tailgate, floor pans
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking, wood trim fading
- **Common damage**: Tailgate rust, suspension bushing wear, exhaust rust
- **Material notes**: Heavy steel (1.2mm), poor rust-proofing, vinyl interior, wood trim

#### **Sadler** (Model ID: 605)
- **Real-world analogs**: 1988-1992 Ford F-150, 1989-1993 Chevrolet C/K 1500, 1990-1994 Dodge Ram 1500
- **Model year**: 1989-1993
- **Era**: VERA_80S (late 80s American full-size pickup)
- **Wear characteristics**:
  - Rust: Severe — bed, fenders, rocker panels, tailgate, cab corners
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Tailgate rust, bed liner wear, suspension sag
- **Material notes**: Heavy steel (1.2mm), poor rust-proofing, vinyl interior

#### **Walton** (Model ID: 478)
- **Real-world analogs**: 1984-1988 Ford F-150, 1985-1989 Chevrolet C/K 1500, 1986-1990 Dodge Ram 1500
- **Model year**: 1985-1989
- **Era**: VERA_80S (mid-80s American full-size pickup)
- **Wear characteristics**:
  - Rust: Severe — bed, fenders, rocker panels, tailgate, cab corners
  - Paint: Fading, chalking, clear coat failure
  - Interior: Vinyl cracking, dashboard cracking
- **Common damage**: Tailgate rust, bed liner wear, suspension sag
- **Material notes**: Heavy steel (1.2mm), poor rust-proofing, vinyl interior

#### **Yosemite** (Model ID: 554)
- **Real-world analogs**: 1992-1996 Ford F-150, 1993-1997 Chevrolet C/K 1500, 1994-1998 Dodge Ram 1500
- **Model year**: 1993-1997
- **Era**: VERA_90S (mid-90s American full-size pickup)
- **Wear characteristics**:
  - Rust: Moderate — bed, fenders, rocker panels, tailgate
  - Paint: Good durability, minor clear coat issues on dark colors
  - Interior: Cloth wear, dashboard fading
- **Common damage**: Tailgate rust, bed liner wear, suspension sag
- **Material notes**: Galvanized steel (1.1mm), multi-stage paint, cloth/vinyl interior

---

## Part 2: System Connection Matrix

### Current Isolated Systems
| System | Current State | Connection Opportunities |
|--------|---------------|-------------------------|
| **Vehicle Spawning** | Random selection from pool | Connect to passenger class, region, time of day |
| **Damage System** | Binary (damaged/undamaged) | Connect to vehicle age, maintenance level, crash severity |
| **Dirt System** | Single float (0-15) | Connect to weather, region, vehicle age, passenger class |
| **Paint System** | Fixed per vehicle | Connect to age, region, maintenance level |
| **Wheel System** | Fixed per vehicle | Connect to age, region, passenger class, maintenance |
| **Lighting System** | Fixed intensity | Connect to time of day, weather, vehicle age |
| **Interior System** | Not rendered | Connect to vehicle age, passenger class, damage level |

### Proposed Connected Systems

#### **Passenger Class → Vehicle Condition**
```
Passenger Class: POOR | WORKING | MIDDLE | UPPER | RICH
                    ↓
Vehicle Condition:
  - Dirt Level: 12-15 | 8-12 | 4-8 | 2-4 | 0-2
  - Damage Level: 0.8-1.0 | 0.5-0.8 | 0.2-0.5 | 0.1-0.3 | 0.0-0.1
  - Wheel Mismatch: 80% | 50% | 20% | 5% | 0%
  - Rust Severity: 0.7-1.0 | 0.4-0.7 | 0.2-0.4 | 0.1-0.2 | 0.0-0.1
  - Paint Fade: 0.6-0.9 | 0.3-0.6 | 0.1-0.3 | 0.05-0.15 | 0.0-0.05
```

#### **Region → Vehicle Pool**
```
Region: GANTON | IDLEWOOD | VINEWOOD | ROCKFORD | RICHMAN
            ↓
Vehicle Pool Weights:
  - Economy Cars: 60% | 50% | 20% | 5% | 2%
  - Muscle Cars: 25% | 30% | 15% | 5% | 3%
  - Sports Cars: 5% | 10% | 30% | 40% | 50%
  - Luxury Cars: 2% | 5% | 20% | 40% | 40%
  - Trucks/SUVs: 8% | 5% | 15% | 10% | 5%
```

#### **Time of Day → Vehicle Behavior**
```
Time: 00:00-06:00 | 06:00-09:00 | 09:00-17:00 | 17:00-22:00 | 22:00-00:00
              ↓
Vehicle Behavior:
  - Headlights: ON | ON | OFF | ON | ON
  - Traffic Density: 10% | 80% | 60% | 90% | 40%
  - Speed: Fast | Moderate | Moderate | Slow | Fast
  - Horn Usage: Low | High | Medium | High | Low
```

#### **Weather → Visual Effects**
```
Weather: CLEAR | CLOUDY | RAIN | FOG | STORM
            ↓
Visual Effects:
  - Wet Surfaces: 0% | 0% | 80% | 20% | 100%
  - Puddle Reflections: 0% | 0% | 60% | 10% | 90%
  - Fog Density: 0% | 20% | 40% | 80% | 60%
  - Lightning: 0% | 0% | 0% | 0% | 30%
  - Wind Speed: 5mph | 10mph | 15mph | 5mph | 25mph
```

---

## Part 3: Feature Roadmap

### Phase 1: Foundation (2024 Q1)
**Goal**: Establish core systems that all future features build upon

#### 1.1 Unified Dirt/Wear System
- **Status**: Partially implemented (dirt level exists, wear does not)
- **Implementation**:
  - Add `CVehicle::m_fWearLevel` (float 0.0-1.0)
  - Connect to passenger class, region, vehicle age
  - Use in shaders for rust, paint fade, interior wear
- **Performance**: Minimal (single float per vehicle, shader math)
- **Dependencies**: None

#### 1.2 Parametric Rubber Shader
- **Status**: Implemented (merged into VehiclePBR_Modern.hlsl)
- **Implementation**:
  - Read roughness/F0/tint from c22/c23
  - Apply dirt/wear tinting
  - Subsurface wrap for realistic rubber
- **Performance**: Minimal (same shader complexity as before)
- **Dependencies**: 1.1 (dirt/wear system)

#### 1.3 Wheel Extender System
- **Status**: Partially implemented (loading exists, rendering does not)
- **Implementation**:
  - Wire `WheelsExtender_RenderWheelCB` to swap atomics
  - Hash-based wheel selection per vehicle
  - Mismatched wheels for poor/beater cars
- **Performance**: Moderate (extra atomic rendering, but only for wheels)
- **Dependencies**: None

#### 1.4 Rust Texture System
- **Status**: Not implemented
- **Implementation**:
  - Use vertex AO to identify rust-prone areas (wheel arches, rocker panels)
  - Tile rust texture based on wear level
  - Blend with base paint in shader
- **Performance**: Low (texture sampling, shader math)
- **Dependencies**: 1.1 (dirt/wear system)

### Phase 2: Visual Enhancement (2024 Q2)
**Goal**: Improve visual fidelity through efficient shader improvements

#### 2.1 Dynamic Sky & Weather
- **Status**: Partially implemented (DynamicSky.hlsl exists)
- **Implementation**:
  - Procedural sky based on time of day, weather
  - Cloud system with parallax
  - Lightning effects during storms
- **Performance**: Moderate (procedural sky rendering)
- **Dependencies**: None

#### 2.2 Wet Surface System
- **Status**: Not implemented
- **Implementation**:
  - Puddle reflections using screen-space reflections
  - Wet surface darkening (shader parameter)
  - Rain droplets on camera
- **Performance**: High (SSR is expensive, but can be optimized)
- **Dependencies**: 2.1 (weather system)

#### 2.3 Improved Lighting
- **Status**: Partially implemented (PBR lighting exists)
- **Implementation**:
  - Area lights (street lamps, car headlights)
  - Volumetric fog
  - God rays
- **Performance**: High (volumetrics are expensive)
- **Dependencies**: None

#### 2.4 Post-Processing
- **Status**: Partially implemented (SMAA, color grading exist)
- **Implementation**:
  - Motion blur
  - Depth of field
  - Bloom
  - Lens flares
- **Performance**: Moderate (post-processing is generally cheap)
- **Dependencies**: None

### Phase 3: System Integration (2024 Q3)
**Goal**: Connect previously isolated game systems

#### 3.1 Passenger Class System
- **Status**: Not implemented
- **Implementation**:
  - Assign class to each pedestrian (POOR, WORKING, MIDDLE, UPPER, RICH)
  - Connect to vehicle spawning, vehicle condition, region
- **Performance**: Minimal (single byte per pedestrian)
- **Dependencies**: None

#### 3.2 Vehicle Age System
- **Status**: Not implemented
- **Implementation**:
  - Assign model year to each vehicle (from database)
  - Connect to wear level, rust severity, paint fade
- **Performance**: Minimal (single short per vehicle)
- **Dependencies**: 1.1 (dirt/wear system)

#### 3.3 Region-Based Spawning
- **Status**: Not implemented
- **Implementation**:
  - Define region boundaries (Ganton, Idlewood, Vinewood, etc.)
  - Adjust vehicle pool weights based on region
  - Connect to passenger class, vehicle condition
- **Performance**: Minimal (region lookup per spawn)
- **Dependencies**: 3.1 (passenger class), 3.2 (vehicle age)

#### 3.4 Time-of-Day System
- **Status**: Partially implemented (timecycle exists)
- **Implementation**:
  - Connect to vehicle behavior (headlights, traffic density)
  - Adjust lighting, weather, pedestrian behavior
- **Performance**: Minimal (time lookup per frame)
- **Dependencies**: None

### Phase 4: Advanced Features (2024 Q4)
**Goal**: Add advanced features that build on Phase 1-3

#### 4.1 Damage System Overhaul
- **Status**: Not implemented
- **Implementation**:
  - Deformable mesh (vertex displacement)
  - Breakable glass
  - Detachable parts (bumpers, doors, wheels)
- **Performance**: High (mesh deformation is expensive)
- **Dependencies**: 1.1 (dirt/wear), 3.2 (vehicle age)

#### 4.2 Interior Rendering
- **Status**: Not implemented
- **Implementation**:
  - Render vehicle interiors (dashboard, seats, steering wheel)
  - Connect to vehicle age, wear level, passenger class
- **Performance**: Moderate (extra geometry, but only for nearby vehicles)
- **Dependencies**: 1.1 (dirt/wear), 3.2 (vehicle age)

#### 4.3 Advanced Materials
- **Status**: Partially implemented (PBR materials exist)
- **Implementation**:
  - Anisotropic materials (brushed metal, hair)
  - Subsurface scattering (skin, wax)
  - Clear coat (car paint, wet surfaces)
- **Performance**: Moderate (shader complexity increase)
- **Dependencies**: None

#### 4.4 Particle System
- **Status**: Not implemented
- **Implementation**:
  - Exhaust smoke
  - Tire smoke
  - Dust/debris
  - Rain/snow
- **Performance**: Moderate (particle rendering is generally cheap)
- **Dependencies**: 2.2 (weather system)

---

## Part 4: Performance Optimization Strategies

### Shader Optimization
1. **Branch elimination**: Use `lerp()` instead of `if/else` where possible
2. **Texture atlasing**: Combine multiple textures into single atlas
3. **Shader permutation reduction**: Use dynamic branching instead of compile-time
4. **Half-precision math**: Use `half` instead of `float` where precision allows
5. **Constant folding**: Pre-compute constants on CPU, upload to shader

### Rendering Optimization
1. **Occlusion culling**: Don't render objects behind walls
2. **LOD system**: Reduce geometry/detail for distant objects
3. **Instancing**: Batch similar objects (trees, street lamps)
4. **Frustum culling**: Don't render objects outside camera view
5. **Depth pre-pass**: Render depth first, then color (reduces overdraw)

### Memory Optimization
1. **Texture compression**: Use DXT/BC compression for textures
2. **Mesh compression**: Use quantized vertex data
3. **Streaming**: Load/unload assets based on distance
4. **Object pooling**: Reuse objects instead of allocating/deallocating
5. **Memory pools**: Pre-allocate memory for common object types

### CPU Optimization
1. **Multithreading**: Split work across multiple CPU cores
2. **Job system**: Queue work for background threads
3. **SIMD**: Use vector instructions for math-heavy operations
4. **Cache optimization**: Organize data for cache locality
5. **Avoid allocations**: Reuse buffers, avoid dynamic allocation in hot paths

---

## Part 5: Scope Management

### In-Scope Features
- Vehicle rendering improvements (paint, wheels, tires, rust)
- Lighting improvements (PBR, area lights, volumetrics)
- Weather system (sky, clouds, rain, fog)
- Post-processing (SMAA, color grading, motion blur)
- System integration (passenger class, vehicle age, region-based spawning)

### Out-of-Scope Features
- New game mechanics (missions, weapons, AI)
- New content (vehicles, pedestrians, buildings)
- Multiplayer/networking
- Scripting/modding tools
- Audio/sound improvements

### Deferred Features (Future Consideration)
- Ray tracing (requires hardware support)
- VR support (requires significant rework)
- Mobile/console ports (requires platform-specific code)
- Modding API (requires significant design work)

---

## Part 6: Success Metrics

### Performance Targets
- **Frame rate**: 60+ FPS on mid-range hardware (GTX 1060 / RX 580)
- **Frame time**: <16.67ms average, <33ms 99th percentile
- **Memory usage**: <2GB VRAM, <4GB RAM
- **Load time**: <30 seconds from main menu to gameplay

### Visual Quality Targets
- **PBR accuracy**: Materials match real-world references
- **Lighting accuracy**: Shadows, reflections, GI match real-world behavior
- **Weather accuracy**: Sky, clouds, rain match real-world appearance
- **Consistency**: Visual quality consistent across all vehicles, regions, times

### User Experience Targets
- **Stability**: <1 crash per 100 hours of gameplay
- **Compatibility**: Works with 95% of existing mods
- **Ease of use**: Simple INI configuration, no manual setup required
- **Documentation**: Clear documentation for all features, settings

---

## Conclusion

SkyGFX Plus is designed to be a **living, evolving graphics engine** that improves GTA SA year-over-year by:
- Building on existing systems rather than replacing them
- Connecting previously isolated game systems
- Restoring cut/unused features from development
- Enhancing visual fidelity through efficient shader improvements
- Maintaining performance on mid-range hardware

This database and roadmap provide a clear path forward, with each phase building on the previous one. By focusing on **system integration** and **visual enhancement**, SkyGFX Plus can make GTA SA feel like a modern game while preserving its original charm and gameplay.

The key to success is **scope management** — avoiding feature creep by focusing on improving what already exists, rather than bolting on unrelated features. This ensures that every hour of development time contributes to a cohesive, polished experience.
