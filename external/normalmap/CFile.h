#pragma once
#include <iostream>

class CFile
{
public:
	static char m_dirName[512];
	static char m_tempName[512];
	static bool m_bInitialised;
	static void Init(char *name);
	static void Shutdown();
	static FILE *Open(char *path, char *mode);
	static void Close(FILE *file);
	static char *MakePath(char *dst, char *subPath);
	static char *MakePath(char *subPath);
};

#define MAKE_PATH(subPath) CFile::MakePath(subPath)