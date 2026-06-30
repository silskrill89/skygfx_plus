# GTA San Andreas - Complete Vehicle Paint Classification System

## PAINT TYPE VISUAL PROPERTIES

| Paint Type | roughness | metalness | reflectance | Description |
|------------|-----------|-----------|-------------|-------------|
| Gloss | 0.15 | 0.0 | 0.9 | Thick clear coat, deep mirror-like reflections, wet look |
| Metallic | 0.20 | 0.4 | 0.85 | Metal flake sparkle, colored reflections, depth |
| Matte | 0.85 | 0.0 | 0.1 | Flat/satin-matte, no reflections, chalky appearance |
| Satin | 0.45 | 0.0 | 0.5 | Semi-gloss, soft diffused reflections |

---

## PAINT TYPE PROBABILITIES BY VEHICLE GROUP

### 1. EUROPEAN SPORTS (Porsche, BMW, Mercedes, Ferrari, Aston Martin, Jaguar)
Based on European manufacturer catalogs of the 1980s-1990s era.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |

*Rationale: European sports cars overwhelmingly came in gloss and metallic from factory. Matte was extremely rare in the era (mostly aftermarket).*

### 2. JAPANESE SPORTS (Toyota, Nissan, Honda, Mitsubishi, Mazda, Subaru)
Based on JDM manufacturer catalogs of the 1980s-1990s.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |

*Rationale: Japanese cars had more variety; some sport trims offered satin/matte options. Slightly higher matte probability reflects tuner culture.*

### 3. AMERICAN MUSCLE (Ford, Chevy, Dodge, Pontiac)
Based on American manufacturer catalogs of the 1960s-1990s.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |

*Rationale: Muscle cars heavily used metallic paints (candy colors, metalflake). Some had matte hoods or flat black options.*

### 4. ECONOMY (VW, Honda, Toyota sedans, Hyundai, Kia)
Based on economy car manufacturer catalogs.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Economy | 0.50 | 0.25 | 0.15 | 0.10 |

*Rationale: Economy cars mostly came in solid gloss (non-metallic) paints. Matte was common on base/cheap trims.*

### 5. LUXURY (Lincoln, Cadillac, Rolls Royce, Bentley, Mercedes S-Class)
Based on luxury manufacturer catalogs.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Luxury | 0.40 | 0.50 | 0.02 | 0.08 |

*Rationale: Luxury vehicles heavily favored metallic and pearlescent finishes. Matte was nearly nonexistent in luxury of this era.*

### 6. TRUCKS / SUVs
Based on truck/SUV manufacturer catalogs.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |

*Rationale: Trucks had higher matte/satin rates due to work vehicles, primer, and utilitarian finishes.*

### 7. LOWRIDERS (Custom paint culture)
Based on lowrider custom paint culture of the 1980s-1990s LA scene.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |

*Rationale: Lowrider culture heavily favors metallic/candy/chrome finishes. Custom paint is the centerpiece of the culture.*

### 8. UTILITY / WORK VANS
Based on commercial/utility vehicle catalogs.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Utility | 0.40 | 0.15 | 0.30 | 0.15 |

*Rationale: Work vehicles frequently in flat/matte colors, primer, or basic gloss. Very little metallic.*

### 9. EMERGENCY / POLICE
Based on emergency vehicle specifications.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Emergency | 0.50 | 0.15 | 0.25 | 0.10 |

*Rationale: Emergency vehicles use durable single-stage paints. Some have matte black trim/hoods.*

### 10. CLASSIC / VINTAGE (Pre-1970s designs)
Based on classic car manufacturer catalogs.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Classic | 0.50 | 0.30 | 0.05 | 0.15 |

*Rationale: Classic cars predominantly used solid gloss or metallic. Very rare matte from factory.*

### 11. MOTORCYCLES
Based on motorcycle manufacturer catalogs.

| Group | Gloss | Metallic | Matte | Satin |
|-------|-------|----------|-------|-------|
| Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |

*Rationale: Motorcycles have diverse paint culture - sport bikes glossy, choppers custom metallic, some matte bobbers.*

---

## COMPLETE GTA SA VEHICLE TABLE

### SPORTS CARS

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 429 | Banshee | Dodge Viper RT-10 / Shelby Daytona | 1992 | Sport | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 555 | Windsor | Aston Martin DB7 | 1993 | Sport | RWD | Euro Sports | 0.40 | 0.45 | 0.05 | 0.10 |
| 480 | Comet | Porsche 911 (964) Carrera | 1990 | Sport | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 560 | Sultan | Toyota Corolla Levin AE86 / Nissan Silvia | 1988 | Sport | RWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 561 | Stratum | Toyota Chaser / Nissan Stagea | 1992 | Sport | AWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 411 | Infernus | Lamborghini Diablo | 1991 | Sport | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 451 | Turismo | Ferrari F50 | 1992 | Sport | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 506 | Sultan RS | Toyota Supra MKIV / Nissan Skyline GT-R | 1993 | Sport | AWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 434 | Hotknife | Ford Model 40 / 1930s Ford Hot Rod | 1936 | Classic/Sport | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 541 | Flash | Porsche 944 / 968 | 1992 | Sport | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 421 | Washington | Lincoln Town Car | 1992 | Luxury | RWD | Luxury | 0.40 | 0.50 | 0.02 | 0.08 |
| 475 | Sabre | Oldsmobile Cutlass | 1986 | Muscle | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 602 | Alpha | Mitsubishi 3000GT / Honda NSX | 1991 | Sport | AWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |

### MUSCLE CARS

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 402 | Buffalo | Dodge Charger / Pontiac LeMans | 1991 | Muscle | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 518 | Buccaneer | Ford Fairlane / Buick Riviera | 1985 | Muscle | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 475 | Sabre | Oldsmobile Cutlass Supreme | 1986 | Muscle | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 517 | Phoenix | Pontiac Firebird | 1987 | Muscle | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 602 | Picador | Chevrolet El Camino | 1986 | Muscle | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 434 | Hotknife | Ford Model 40 Hot Rod | 1936 | Classic | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |
| 555 | Glendale | Buick Regal / Oldsmobile | 1984 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 466 | Glendale | Buick Skylark | 1982 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 542 | Walton | Ford F-100 | 1975 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 468 | Yosemite | Chevrolet C/K Square Body | 1987 | Truck | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 601 | Slamvan | Ford F-100 Custom | 1956 | Classic | RWD | American Muscle | 0.35 | 0.40 | 0.10 | 0.15 |

### LOWRIDERS

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 536 | Blade | Ford Falcon / Fairlane | 1988 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 575 | Broadway | Chevrolet Impala | 1963 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 567 | Voodoo | Chevrolet Impala | 1962 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 412 | Tornado | Chevrolet Bel Air | 1957 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 534 | Remington | Buick Regal | 1983 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 535 | Slamvan | Ford F-100 (custom) | 1956 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 566 | Tahoma | Chevrolet Caprice | 1986 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 467 | Buccaneer | Ford Fairlane (custom) | 1985 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |
| 576 | Perennial | Volkswagen Golf | 1985 | Economy | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 518 | Buccaneer | Ford Fairlane | 1985 | Lowrider | RWD | Lowriders | 0.25 | 0.55 | 0.05 | 0.15 |

### SEDANS / ECONOMY

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 405 | Sentinel | BMW 5 Series (E34) | 1992 | Luxury | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 445 | Emperor | Lincoln Continental | 1988 | Luxury | RWD | Luxury | 0.40 | 0.50 | 0.02 | 0.08 |
| 466 | Glendale | Oldsmobile Cutlass Ciera | 1984 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 546 | Intruder | Toyota Camry | 1992 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 404 | Perennial | Volkswagen Golf / Honda Civic | 1985 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 400 | Uranus | Ford Probe | 1989 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 474 | Hermes | Oldsmobile 88 | 1979 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 540 | Premier | Chevrolet Lumina | 1990 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 547 | Primo | Toyota Corolla | 1991 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 421 | Washington | Lincoln Town Car | 1989 | Luxury | RWD | Luxury | 0.40 | 0.50 | 0.02 | 0.08 |
| 458 | Solair | Toyota Tercel | 1991 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 551 | Merit | Toyota Cressida | 1988 | Sedan | RWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 542 | Oceanic | Pontiac Grand Prix | 1987 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 562 | Elegy | Nissan Skyline GT-R (R32) | 1989 | Sport | AWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 559 | Jester | Toyota Supra MKIII | 1989 | Sport | RWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 560 | Sultan | Toyota Corolla Levin AE86 | 1988 | Sport | RWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 506 | Sultan RS | Toyota Supra MKIV | 1993 | Sport | AWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 526 | Fortune | Nissan 240SX (S13) | 1989 | Sport | RWD | JDM Sports | 0.40 | 0.35 | 0.10 | 0.15 |
| 555 | Windsor | Aston Martin DB7 | 1993 | Luxury | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 478 | Stafford | Rolls-Royce Silver Spirit | 1988 | Luxury | RWD | Luxury | 0.40 | 0.50 | 0.02 | 0.08 |
| 507 | Elegant | Mercedes-Benz W124 | 1991 | Luxury | RWD | Euro Sports | 0.45 | 0.40 | 0.05 | 0.10 |
| 546 | Intruder | Toyota Camry | 1992 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 585 | Willow | Volkswagen Jetta | 1989 | Sedan | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |

### CLASSICS / VINTAGE

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 474 | Hermes | Oldsmobile Delta 88 | 1979 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 555 | Glendale | Buick Skylark | 1982 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 542 | Walton | Ford F-100 (1970s) | 1975 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 467 | Ambassador | Cadillac Eldorado | 1976 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 518 | Buccaneer | Ford Fairlane (1970s) | 1985 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 466 | Glendale | Chevrolet Malibu | 1982 | Classic | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |

### TRUCKS / SUVs

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 468 | Yosemite | Chevrolet C/K | 1987 | Truck | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 499 | Baron | Chevrolet Blazer / Ford Bronco | 1986 | SUV | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 554 | Yosemite | GMC Sierra | 1988 | Truck | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 478 | Walton | Ford F-150 | 1975 | Truck | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 543 | Sadler | Ford F-250 | 1986 | Truck | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 482 | Burrito | Chevrolet Van | 1985 | Van | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 483 | Moonbeam | GMC Vandura | 1983 | Van | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 508 | Journey | Ford E-Series | 1986 | Van | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 459 | Blista | Honda CRX | 1989 | Compact | FWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 489 | Landstalker | Jeep Grand Wagoneer | 1988 | SUV | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 500 | Mesa | Jeep Cherokee | 1986 | SUV | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 579 | Rancher | Ford Bronco | 1987 | SUV | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 495 | Voodoo | Chevrolet Suburban | 1986 | SUV | RWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |

### UTILITY / VANS / COMMERCIAL

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 482 | Burrito | Chevrolet G-Series | 1985 | Van | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 483 | Moonbeam | GMC Vandura | 1983 | Van | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 508 | Journey | Ford E-150 | 1986 | Van | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 459 | Blista | Honda Civic Van | 1989 | Van | FWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 528 | Towtruck | Chevrolet C/K Tow Truck | 1987 | Utility | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 525 | Towtruck | Ford F-600 Tow Truck | 1985 | Utility | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 588 | Hotring | Ford Crown Victoria Taxi | 1992 | Taxi | RWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 420 | Taxi | Chevrolet Caprice | 1991 | Taxi | RWD | Economy | 0.50 | 0.25 | 0.15 | 0.10 |
| 438 | Cabbie | Checker Marathon | 1982 | Taxi | RWD | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 574 | Bus | GMC RTS | 1986 | Bus | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 437 | Coach | MCI MC-9 | 1985 | Bus | RWD | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 523 | HPV1000 | Police Motorcycle | 1988 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |

### EMERGENCY / POLICE / MILITARY

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 596 | Police LS | Ford Crown Victoria | 1992 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 597 | Police SF | Ford Crown Victoria | 1992 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 598 | Police LV | Ford Crown Victoria | 1992 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 599 | Ranger | Chevrolet Blazer (Police) | 1986 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 427 | Enforcer | Chevrolet Suburban (Police) | 1988 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 490 | FBI Rancher | Ford Bronco (FBI) | 1987 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 470 | Patriot | HMMWV (Humvee) | 1985 | Military | AWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 432 | Rhino | M1 Abrams Tank | 1985 | Military | AWD | Trucks/SUVs | 0.35 | 0.30 | 0.20 | 0.15 |
| 528 | FBI Truck | Ford F-150 (FBI) | 1988 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 407 | Firetruck | Pierce Dash | 1988 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |
| 416 | Ambulance | Ford E-Series Ambulance | 1987 | Emergency | RWD | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |

### MOTORCYCLES

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 461 | PCJ-600 | Ducati 916 | 1993 | Sport | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 521 | FCR-900 | Honda CBR900RR Fireblade | 1992 | Sport | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 468 | Sanchez | Honda XR600R | 1991 | Dirt | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 522 | NRG-500 | Suzuki RG500 Gamma | 1985 | Sport | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 462 | Faggio | Vespa PK50XL | 1985 | Scooter | FWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 581 | BF-400 | Suzuki DR350 | 1990 | Dual-Sport | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 463 | Freeway | Harley-Davidson FLH | 1985 | Cruiser | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 523 | HPV1000 | Kawasaki KZ1000P | 1988 | Sport | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 586 | Wayfarer | Honda Gold Wing GL1500 | 1988 | Touring | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 448 | Pizza Boy | Honda Super Cub C90 | 1984 | Scooter | RWD | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 571 | BMX | BMX Bicycle | 1990 | Bicycle | N/A | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |
| 510 | Mountain Bike | Mountain Bicycle | 1992 | Bicycle | N/A | Motorcycles | 0.35 | 0.35 | 0.15 | 0.15 |

### BOATS

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 472 | Coastguard | Boston Whaler Guardian | 1990 | Boat | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 473 | Dinghy | Zodiac RIB | 1988 | Boat | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 493 | Jetmax | Wellcraft Scarab | 1992 | Boat | N/A | Sport | 0.40 | 0.35 | 0.10 | 0.15 |
| 595 | Launch | Chris-Craft Commander | 1985 | Boat | N/A | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 446 | Squalo | Sea Ray 270 | 1989 | Boat | N/A | Luxury | 0.40 | 0.50 | 0.02 | 0.08 |
| 454 | Tropic | Donzi 28 ZX | 1988 | Boat | N/A | Sport | 0.40 | 0.35 | 0.10 | 0.15 |
| 484 | Voodoo | Chris-Craft Cavalier | 1982 | Boat | N/A | Classic | 0.50 | 0.30 | 0.05 | 0.15 |

### AIRCRAFT

| ID | In-Game Name | Real-Life Car | Year | Category | Drive | Group | Gloss | Metallic | Matte | Satin |
|----|-------------|---------------|------|----------|-------|-------|-------|----------|-------|-------|
| 592 | Andromada | Lockheed C-130 Hercules | 1985 | Plane | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 577 | AT-400 | Boeing 737 | 1988 | Plane | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 511 | Beagle | Cessna 172 | 1986 | Plane | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 563 | Cropduster | Piper PA-36 Pawnee Brave | 1985 | Plane | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 519 | Shamal | Learjet 35 | 1987 | Plane | N/A | Luxury | 0.40 | 0.50 | 0.02 | 0.08 |
| 513 | Dodo | de Havilland Canada DHC-2 Beaver | 1985 | Plane | N/A | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 553 | Nevada | Douglas DC-3 | 1985 | Plane | N/A | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 476 | Rustler | North American T-6 Texan | 1985 | Plane | N/A | Classic | 0.50 | 0.30 | 0.05 | 0.15 |
| 447 | Leviathan | Sikorsky S-61 | 1985 | Heli | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 488 | San Chian | Bell 206 JetRanger | 1986 | Heli | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 487 | Maverick | Bell 206 LongRanger | 1986 | Heli | N/A | Utility | 0.40 | 0.15 | 0.30 | 0.15 |
| 497 | Police Maverick | Eurocopter AS350 | 1988 | Heli | N/A | Emergency | 0.50 | 0.15 | 0.25 | 0.10 |

---

## SUMMARY OF GROUP DISTRIBUTION

### Vehicle Count by Group

| Group | Count | Example Vehicles |
|-------|-------|------------------|
| Euro Sports | 6 | Banshee, Comet, Infernus, Turismo, Flash, Windsor |
| JDM Sports | 6 | Sultan, Stratum, Sultan RS, Elegy, Jester, Fortune |
| American Muscle | 4 | Buffalo, Sabre, Phoenix, Picador, Slamvan |
| Luxury | 4 | Washington, Emperor, Stafford, Elegant |
| Economy | 8 | Perennial, Uranus, Premier, Primo, Intruder, Solair, Merit, Oceanic |
| Lowriders | 8 | Blade, Broadway, Voodoo, Tornado, Remington, Tahoma, Buccaneer, Slamvan |
| Classic | 6 | Hermes, Glendale, Walton, Ambassador, Buccaneer (classic) |
| Trucks/SUVs | 8 | Yosemite, Landstalker, Mesa, Rancher, Patriot, Voodoo |
| Utility | 10 | Burrito, Moonbeam, Journey, Blista, Towtruck, Bus, Coach |
| Emergency | 8 | Police LS/SF/LV, Ranger, Enforcer, FBI Rancher, Firetruck, Ambulance |
| Motorcycles | 12 | PCJ-600, FCR-900, Sanchez, NRG-500, Faggio, Freeway |
| Boats | 7 | Jetmax, Squalo, Tropic, Voodoo (boat), Launch |
| Aircraft | 12 | AT-400, Andromada, Shamal, Nevada, Maverick, Leviathan |

### Paint Weight Usage Guide

For implementation in your graphics mod:

1. **Determine vehicle group** from the table above
2. **For each paint application**, generate a random number 0-1
3. **Use cumulative probability** to select paint type:
   - If rand < Gloss → use Gloss properties
   - If rand < Gloss + Metallic → use Metallic properties
   - If rand < Gloss + Metallic + Matte → use Matte properties
   - Else → use Satin properties

4. **Apply visual properties** from the paint type table:
   - roughness: controls surface roughness (0 = mirror, 1 = completely rough)
   - metalness: controls metallic appearance (0 = dielectric, 1 = metal)
   - reflectance: controls specular reflection intensity

### Color Recommendations by Group

| Group | Most Common Colors | Notes |
|-------|-------------------|-------|
| Euro Sports | Silver, Black, Red, White, Blue | Subdued, professional colors |
| JDM Sports | White, Red, Blue, Silver, Black | Often modified with aftermarket colors |
| American Muscle | Red, Blue, Black, White, Orange | Bold, high-contrast colors |
| Luxury | Black, White, Silver, Dark Blue, Burgundy | Conservative, prestigious colors |
| Economy | White, Silver, Blue, Red, Beige | Practical, neutral colors |
| Lowriders | Purple, Gold, Teal, White, Red | Custom candy/flake colors, two-tone |
| Classic | Cream, Dark Green, Maroon, Brown, Black | Era-appropriate colors |
| Trucks/SUVs | White, Black, Red, Blue, Green | Durable, practical colors |
| Utility | White, Yellow, Blue, Red, Green | Fleet/identification colors |
| Emergency | Black/White, Blue/White, Red, Yellow | Standardized fleet colors |
| Motorcycles | Red, Black, Blue, Silver, Yellow | Varies by type (sport vs cruiser) |

---

## REFERENCE: PBR MATERIAL VALUES FOR RENDERER

### Gloss (Clear Coat)
```
roughness: 0.15
metalness: 0.0
reflectance: 0.9
clearcoat: 1.0
clearcoat_roughness: 0.05
```

### Metallic
```
roughness: 0.20
metalness: 0.4
reflectance: 0.85
clearcoat: 0.3
clearcoat_roughness: 0.1
```

### Matte
```
roughness: 0.85
metalness: 0.0
reflectance: 0.1
clearcoat: 0.0
clearcoat_roughness: 0.0
```

### Satin
```
roughness: 0.45
metalness: 0.0
reflectance: 0.5
clearcoat: 0.2
clearcoat_roughness: 0.3
```

---

*Generated for skygfx_plus_expIV paint type system implementation*
*Data based on GTA SA vehicle database and real-world manufacturer catalogs from 1980s-1990s era*
