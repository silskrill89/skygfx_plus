# Vehicle Classification

#vehicles #classification

## Overview
Per-vehicle class system for glass tinting, paint type detection, and rendering customization. Classification based on vehicle model ID lookup.

## Vehicle Classes
| Class | Tint Color | Examples |
|-------|-----------|----------|
| Taxi | Dark yellow | Taxi, Police |
| Police | Dark blue/black | Police, FBI Rancher |
| Gang | Various | Gang vehicles |
| Lowrider | Clear/light | Savanna, Blade |
| Casual | Blue | Common traffic |

## Detection Methods
- **Model index lookup** — `FindPlayerVehicle()` model ID mapped to class
- **Material properties** — `hasAlpha && material->color.alpha < 200` → glass
- **Chrome detection** — By shininess/specular, not model IDs

## INI Control
```ini
glassTintEnable=1
```

## See Also
- [[Glass Shader]] — Tint rendering details
- [[Vehicle Pipeline]] — All pipe modes
