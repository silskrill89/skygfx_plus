#include "dff\Clump.h"
#include <conio.h>

bool GetLine(char *buf, FILE *file)
{
	while(fgets(buf, 256, file))
	{
		if(buf[0] != '#' && buf[0] != '\n')
			return true;
	}
	return false;
}

gtaRwAtomic *AtomicByGeomIndex(gtaRwClump &clump, unsigned int idx)
{
	if(clump.geometryList.geometryCount > 0)
	{
		for(int i = 0; i < clump.numAtomics; i++)
		{
			if(clump.atomics[i].geometryIndex == idx)
				return &clump.atomics[i];
		}
	}
	return NULL;
}

void AtomicSetupNM(gtaRwAtomic &atomic)
{
	if(atomic.Extension.rights[0].enabled && atomic.Extension.rights[0].pluginId == rwID_NORMALMAP)
		return;
	if(atomic.Extension.rights[1].enabled && atomic.Extension.rights[1].pluginId == rwID_NORMALMAP)
		return;
	if(atomic.Extension.rights[0].enabled && atomic.Extension.rights[0].pluginId == rwID_MATFX)
		atomic.Extension.rights[0].Destroy();
	if(atomic.Extension.rights[1].enabled && atomic.Extension.rights[1].pluginId == rwID_MATFX)
		atomic.Extension.rights[1].Destroy();
	atomic.Extension.matFx.Destroy();
	if(atomic.Extension.rights[0].enabled)
	{
		if(atomic.Extension.rights[0].pluginId == rwID_SKIN)
			atomic.Extension.rights[1].Initialise(rwID_NORMALMAP, 2);
		else
			atomic.Extension.rights[1].Initialise(rwID_NORMALMAP, 1);
	}
	else
		atomic.Extension.rights[0].Initialise(rwID_NORMALMAP, 1);
}

void GeometrySetupNM(gtaRwGeometry &geometry)
{
	if(geometry.textured2 || geometry.GetTexCoordsCount() > 1)
	{
		for(int i = 1; i < geometry.GetTexCoordsCount(); i++)
		{
			gtaMemFree(geometry.texCoords[i]);
			geometry.texCoords[i] = NULL;
		}
		geometry.textured2 = false;
		geometry.numTexCoordSets = 1;
	}
}

void MaterialSetupNM(gtaRwMaterial &material, bool envMap, char *nrmname, char *nrmname_a, unsigned char nrm_f,
					 unsigned char nrm_u, unsigned char nrm_v, unsigned char nrm_m, char *rflname, char *rflname_a,
					 unsigned char rfl_f, unsigned char rfl_u, unsigned char rfl_v, unsigned char rfl_m,
					 float rfl_intensity, bool rfl_modulate)
{
	material.Extension.specMap.Destroy();
	material.Extension.envMap.Destroy();
	material.Extension.matFx.Destroy();
	material.Extension.normalMap.Initialise(true, envMap, rfl_intensity, rfl_modulate);
	material.Extension.normalMap.normalMapTexture.Initialise((gtaRwTextureFilterMode)nrm_f, 
		(gtaRwTextureAddressMode)nrm_u, (gtaRwTextureAddressMode)nrm_v, nrm_m, nrmname, nrmname_a);
	if(envMap)
	{
		material.Extension.normalMap.envMapTexture.Initialise((gtaRwTextureFilterMode)rfl_f, 
			(gtaRwTextureAddressMode)rfl_u, (gtaRwTextureAddressMode)rfl_v, rfl_m, rflname, rflname_a);
	}
}

int main()
{
	FILE *file = fopen("nm.txt", "rt");
	if(file)
	{
		char line[256];
		while(GetLine(line, file))
		{
			char filename[512];
			char texname[32];
			char nrmname[32];
			char nrmname_a[32];
			char rflname[32];
			char rflname_a[32];
			filename[0] = '\0';
			texname[0] = '\0';
			nrmname[0] = '\0';
			nrmname_a[0] = '\0';
			rflname[0] = '\0';
			rflname_a[0] = '\0';
			unsigned int nrm_f = 6, nrm_u = 1, nrm_v = 1, rfl_f = 6, rfl_u = 1, rfl_v = 1, nrm_m = 0, rfl_m = 0; 
			float rfl_intensity = 0.0f;
			unsigned int rfl_modulate = 0;
			int count = sscanf(line, "%s %s %s %s %d %d %d %d %s %s %d %d %d %d %f %d", filename, texname, nrmname, nrmname_a, &nrm_f, &nrm_u, &nrm_v, &nrm_m,
				rflname, rflname_a, &rfl_f, &rfl_u, &rfl_v, &rfl_m, &rfl_intensity, &rfl_modulate);
			if(nrmname[0] == '-')
				nrmname[0] = '\0';
			if(nrmname_a[0] == '-')
				nrmname_a[0] = '\0';
			if(rflname[0] == '-')
				rflname[0] = '\0';
			if(rflname_a[0] == '-')
				rflname_a[0] = '\0';
			FILE *file = fopen(filename, "rb");
			if(file)
			{
				fclose(file);
				printf("file \"%s\"\n", filename);
				gtaRwClump clump;
				gtaRwStream *stream = gtaRwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, filename);
				if(stream)
				{
					if(clump.StreamRead(stream))
					{
						gtaRwBool matFound = false;
						gtaRwStreamClose(stream);
						if(clump.geometryList.geometryCount > 0)
						{
							for(int g = 0; g < clump.geometryList.geometryCount; g++)
							{
								for(int m = 0; m < clump.geometryList.geometries[g].matList.materialCount; m++)
								{
									gtaRwMaterial &mtl = clump.geometryList.geometries[g].matList.materials[m];
									if(mtl.textured && mtl.texture.name.string)
									{
										if(!strcmp(mtl.texture.name.string, texname))
										{
											MaterialSetupNM(mtl, count > 8, nrmname, nrmname_a, nrm_f, nrm_u, nrm_v, nrm_m, rflname, rflname_a,
												rfl_f, rfl_u, rfl_v, rfl_m, rfl_intensity, rfl_modulate);
											gtaRwAtomic *atm = AtomicByGeomIndex(clump, g);
											if(atm)
												AtomicSetupNM(*atm);
											GeometrySetupNM(clump.geometryList.geometries[g]);
											if(count > 8)
												printf("Normalmap attached to \"%s\" (%s, %s - %f)\n", texname, nrmname, rflname, rfl_intensity);
											else
												printf("Normalmap attached to \"%s\" (%s)\n", texname, nrmname);
											matFound = true;
										}
									}
								}
							}
						}
						else
						{
							for(int a = 0; a < clump.numAtomics; a++)
							{
								bool atomicHasNM = false;
								if(clump.atomics[a].internalGeometry)
								{
									for(int m = 0; m < clump.atomics[a].internalGeometry->matList.materialCount; m++)
									{
										gtaRwMaterial &mtl = clump.atomics[a].internalGeometry->matList.materials[m];
										if(mtl.textured && mtl.texture.name.string)
										{
											if(!strcmp(mtl.texture.name.string, texname))
											{
												MaterialSetupNM(mtl, count > 8, nrmname, nrmname_a, nrm_f, nrm_u, nrm_v, nrm_m, rflname, rflname_a,
													rfl_f, rfl_u, rfl_v, rfl_m, rfl_intensity, rfl_modulate);
												atomicHasNM = true;
												if(count > 8)
													printf("Normalmap attached to \"%s\" (%s, %s - %.2f)\n", texname, nrmname, rflname, rfl_intensity);
												else
													printf("Normalmap attached to \"%s\" (%s)\n", texname, nrmname);
												matFound = true;
											}
										}
									}
								}
								if(atomicHasNM)
								{
									GeometrySetupNM(*clump.atomics[a].internalGeometry);
									AtomicSetupNM(clump.atomics[a]);
								}
							}
						}
						stream = gtaRwStreamOpen(rwSTREAMFILENAME, rwSTREAMWRITE, filename);
						if(stream)
						{
							clump.StreamWrite(stream);
							gtaRwStreamClose(stream);
						}
						clump.Destroy();
						if(!matFound)
							printf("Can't find material with texture \"%s\"\n", texname);
					}
					else
					{
						printf("Error when reading dff model\n");
						gtaRwStreamClose(stream);
					}
				}
			}
			else
				printf("File \"%s\" not found\n", filename);
		}
	}
	else
		printf("File \"nm.txt\" not found\n");
	getch();
	return TRUE;
}