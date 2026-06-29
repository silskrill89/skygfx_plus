#include "CFile.h"
#include <Windows.h>

char CFile::m_dirName[512];
char CFile::m_tempName[512];
bool CFile::m_bInitialised;

void CFile::Init(char *name)
{
	HMODULE hModule = GetModuleHandle(name);
	GetModuleFileName(hModule, m_dirName, 512);
	if (strrchr(m_dirName, '\\')) strrchr(m_dirName, '\\')[1] = '\0';
	m_bInitialised = true;
}

void CFile::Shutdown()
{

}

FILE *CFile::Open(char *path, char *mode)
{
	if(!m_bInitialised)
	{
		MessageBox(NULL, "CFile::m_bInitialised - FALSE", "CFile::Open", 0);
		return NULL;
	}
	char name[512];
	strcpy_s(name, m_dirName);
	strcat_s(name, path);
	return fopen(name, mode);
}

void CFile::Close(FILE *file)
{
	fclose(file);
}

char *CFile::MakePath(char *dst, char *subPath)
{
	strcpy(dst, m_dirName);
	strcat(dst, subPath);
	return dst;
}

char *CFile::MakePath(char *subPath)
{
	strcpy_s(m_tempName, m_dirName);
	strcat_s(m_tempName, subPath);
	return m_tempName;
}