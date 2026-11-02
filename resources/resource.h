// resource.h - Sequential IDs starting from 100

// Grass + Utility (100-109)
#define IDR_GRASSPS                 100
#define IDR_SIMPLEPS                101
#define IDR_VEHICLEVS               102

// GTAIV (110-119)
#define IDR_GTAIVPS                 110
#define IDR_GTAIVFPVS               111
#define IDR_GTAIVFPPS               112

// SMAA + SSAO (120-139)
#define IDR_SSAOPS                  120
#define IDR_SMAAPS                  121
#define IDR_SMAAEDGEPS              122
#define IDR_SMAABLENDWEIGHTPS       123
#define IDR_SMAABLENDNEIGHBORPS     124
#define IDR_SMAAEDGENORMALPS        125
#define IDR_SMAAEDGEDEPTHPS         126
#define IDR_SMAATEMPPS              127
#define IDR_SMAAEDGECOMBINEDPS      128
#define IDR_SMAAEDGEMOTIONDEPTHPS   129
#define IDR_SSAO_VERTEXDEPTH        130
#define IDR_SSAO_TEMPORAL_PS        131
#define IDR_SSAO_BILATERALBLUR_PS   132
#define IDR_SSAO_UPSAMPLE_PS        133

// PostFX (140-159)
#define IDR_MOTIONBLUR_BURNOUT      140
#define IDR_COLORFILTER_CROSSMIX    141
#define IDR_SSS_BLUR                142
#define IDR_NORMALBUFFERPS          143
#define IDR_PIPECHAINPS             144

// Grading (160-179)
#define IDR_GRADINGPS               160
#define IDR_CONTRASTPS              161
#define IDR_BLURPS                  162
#define IDR_RADIOSITYPS             163
#define IDR_VCTRAILSPS              164
#define IDR_MODERNCOLORFILTERPS     165
#define IDR_TONEMAPPASS             166

// PBR / Modern (200-210)
#define IDR_VEHICLEPAINT_GTAIV      200
#define IDR_WATER_PARALLAX          201
#define IDR_WATER_VS                202
#define IDR_VEHICLEPBR_MODERN       203
#define IDR_GLASS_VEHICLE           204
#define IDR_RUBBER_VEHICLE          205
#define IDR_CARPAINT_REFL           206
#define IDR_PBR_LIGHTING            207
#define IDR_CLAMP                   208
#define IDR_DYNAMICSKY              209
#define IDR_SKINPBR                 210
#define IDR_RUBBER_VEHICLE_MODERN   240

// Water style variants (Water_Parallax.hlsl multi-entry: main_iv / main_v)
#define IDR_WATER_IV                241
#define IDR_WATER_V                 242

// Vehicle legacy (211-219)
#define IDR_VEHICLEPBRVS             211
#define IDR_PS2ENVSPECFXPS           212
#define IDR_PS2CARFXVS              213
#define IDR_SPECCARFXVS             214
#define IDR_SPECCARFXPS             215
#define IDR_XBOXCARVS               216
#define IDR_LEEDSCARFXVS            217
#define IDR_MOBILEVEHICLEVS         218
#define IDR_MOBILEVEHICLEPS         219

// GTAIV vehicle/building (220-221)
#define IDR_GTAIVVEHICLEVS          220
#define IDR_GTAIVVEHICLEPS          221

// Building legacy (222-234)
#define IDR_PS2BUILDINGVS           222
#define IDR_PS2BUILDINGFXVS         223
#define IDR_PS2BUILDINGWINDVS       224
#define IDR_XBOXBUILDINGVS          225
#define IDR_XBOXBUILDINGPS          226
#define IDR_XBOXBUILDINGNOISEDETAILPS 227
#define IDR_XBOXBUILDINGWINDVS      228
#define IDR_SPHEREBUILDINGVS        229
#define IDR_SIMPLEDETAILPS          230
#define IDR_SIMPLEDETAILNOISEDETAILPS 231
#define IDR_SIMPLEFOGPS             232
#define IDR_GTAIVBUILDINGVS         233
#define IDR_GTAIVBUILDINGPS         234
#define IDR_CUSTOMBUILDINGVS        235
#define IDR_NORMMAPBUILDINGPS       236
#define IDR_NORMMAPVEHICLEPS        237
#define IDR_BUILDINGPBRVS           238
#define IDR_BUILDINGPBRPS           239

// Atmospheric (250-259)
#define IDR_HEIGHTFOG               250
#define IDR_GODRAYS                 251

// Velocity
#define IDR_VELOCITYRECONSTRUCT     252

// Unified tonemap (frame-adaptive exposure)
#define IDR_LUMINANCEREDUCE         253
#define IDR_LUMINANCEADAPT          254

// New effects (260-275)
#define IDR_FXAA3                   260
#define IDR_GRAIN                   261
#define IDR_CA                      262
#define IDR_VIGNETTE                263
#define IDR_VECTORMOTIONBLUR        264
#define IDR_DOF_COC                 265
#define IDR_DOF_BOKEH               266
#define IDR_DOF_TENT                267
#define IDR_DOF_COMBINE             268
#define IDR_TONEMAPLUT              269
#define IDR_BLOOM_EXTRACT           270
#define IDR_BLOOM_DOWNSAMPLE        271
#define IDR_BLOOM_UPSAMPLE          272
#define IDR_BLOOM_COMPOSITE         273
#define IDR_SUNSHAFTS               274
#define IDR_PEDPBRVS                275

// W7-C: mirror distortion fix composite (Mirror_Composite.hlsl -> cso/Mirror_Composite.cso)
// NOTE: 276 was staged for W7-D and IS NOW LIVE as IDR_WATER_UNDERWATER
// (defined below; loaded by postfx_underwater.cpp) — do not reuse.
// 278 is now IDR_SIMPLENOISEDETAILPS (defined at the end of this file).
#define IDR_MIRROR_COMPOSITE         277

// Underwater volume fog + SSS (W7-D)
#define IDR_WATER_UNDERWATER        276
#define IDR_TREEALPHA               279	// W8-2 TreeAlpha.cso (276=W7-D, 277=W7-C, 278=SimpleNoiseDetail below, 280=W8-3)

// Legacy simple (non-detail-map) noise-detail building PS — aap SkyGfx parity
#define IDR_SIMPLENOISEDETAILPS     278

// W8 clouds (CloudsPS.hlsl). 280 stays reserved for W8-3 per the note above.
//   main_clouds      -> cso/CloudsPS.cso      (auto-rglob /E main alias)
//   main_cloudsSky   -> cso/cloudsSkyPS.cso   (fast_build.py multi_entry row)
#define IDR_CLOUDSSKYPS             283
#define IDR_CLOUDPS                 284

// IV grade (IVGrade.hlsl -> cso/IVGrade.cso). The resource is wired for
// CSO loading, but the shipped g_ivGradePSBytes array stays the default
// for this build: the reconstructed CSO is NOT byte-identical (see
// EnsureIVGradePS in postfx_colourfilter.cpp).
#define IDR_IVGRADEPS               285

// m0336 GPU particle lane (particles_gpu.cpp + WakeParticlePS.hlsl).
// ps_3_0 sprite PS for the quad-batch particle path (boat wake first slice).
#define IDR_WAKEPARTICLEPS          286
