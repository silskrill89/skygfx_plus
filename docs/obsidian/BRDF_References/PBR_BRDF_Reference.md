# SkyGFX Plus - PBR BRDF Reference

## Sources
- Disney Principled BRDF (Burley 2012): https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
- Trowbridge-Reitz / "GGX" (Walter et al. 2007): https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
- JCGT Practical BRDF: https://www.jcgt.org/published/0007/04/01/paper.pdf
- Stanford CS348b Reflection: http://graphics.stanford.edu/courses/cs348b-18-spring-content/lectures/12_reflection2/12_reflection2_slides.pdf
- Illinois CS418 Disney BRDF: https://cs418.cs.illinois.edu/website/text/disney-brdf.html
- Matt Pharr on Trowbridge-Reitz naming: https://pharr.org/matt/blog/2022/05/06/trowbridge-reitz
- O3DE Enhanced PBR: GGX/Smith/Schlick/Burley (ported from GTAIVForwardPlus_ps.hlsl)
- GTA V Graphics Study (Adrian Courreges): https://www.adriancourreges.com/blog/2015/11/02/gta-v-graphics-study-part-2/

## BRDF Components Used in VehiclePBR_Modern

### 1. Trowbridge-Reitz (GGX) Normal Distribution
```
D(h) = alpha^2 / (pi * ((n.h)^2 * (alpha^2 - 1) + 1)^2)
where alpha = roughness^2
```

### 2. Smith Correlated Visibility (Height-Correlated)
```
V = G1(L) * G1(V) / (4 * n.L * n.V)
G1(v) = 2 * n.v / (n.v + sqrt(alpha^2 + (1-alpha^2) * n.v^2))
Simplified: V = 0.5 / (sqrt(GGXV) * sqrt(GGXL) + epsilon)
where GGXV = n.V^2 * (1-alpha^2) + alpha^2
      GGXL = n.L^2 * (1-alpha^2) + alpha^2
```

### 3. Schlick Fresnel
```
F(cosTheta) = F0 + (1 - F0) * (1 - cosTheta)^5
F0 = lerp(reflectance, baseColor, metalness)
```

### 4. Disney Burley Diffuse (Renormalized)
```
fd = baseColor / pi * (1 + (F90-1) * (1-n.L)^5) * (1 + (F90-1) * (1-n.V)^5)
where F90 = 0.5 + 2 * roughness * (L.H)^2
```

### 5. Energy Conservation
```
Diffuse energy = (1 - F) * (1 - metalness)
Specular energy = F
Total energy <= 1 (energy conserving)
```

## ps_3_0 Constraints
- Max 64 arithmetic instructions, 32 texture instructions
- No dynamic branching (use if/else carefully)
- 4 texture stages max in our setup (s0=diffuse, s1=env, s2=mask, s3=IBL)

## Key Registers (VehiclePBR_Modern)
```
c0  = surfProps {ambient, 0, diffuse, prelit}     (from env pipe setup)
c1  = fxParams {fxSwitch, shininess, specularity, lightmult}
c2  = eyePos
c3  = iblParams {roughness, metalness, 0, 0}
c4  = cloudShadow {sunDirX, sunDirY, sunDirZ, time}
c5  = directCol (DO NOT OVERWRITE)
c6-c11 = lightCol[6] (DO NOT OVERWRITE)
c12 = directDir (DO NOT OVERWRITE)
c13-c18 = lightDir[6] (DO NOT OVERWRITE)
c19 = matCol (DO NOT OVERWRITE)
c22 = pbrParams {roughness, metalness, reflectance, envFresnel}
```

## GTA V Rendering Insights (from Courreges study)
- Environment cubemap: low-poly scene, no characters/cars
- Planar reflection: 240x120 for water/mirrors
- Deferred lighting: screen-space light volumes (tessellated octahedra)
- Each light pixel reads G-buffer (depth, normal, specular/glossiness)
- Light streaks: 12 rotated quads around sun
- Lens flares: 70 sprites along sun-center axis
- DoF: Signed CoC map, 2-pass blur via compute shader
- Film grain + vignetting for "Wasted" screen

## Cloud Shadow Optimization Notes
- Current: 4-octave 3D FBM (expensive for ps_3_0)
- Future: Use 2D noise at world XZ, single octave for speed
- Or: Pre-render cloud shadow to texture (like GTA V's shadow cascade)
