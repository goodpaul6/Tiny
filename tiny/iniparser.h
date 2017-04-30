#pragma once

#include <stdbool.h>

#include "tiny.h"

typedef struct
{
	char* name;

	int count;
	char** keys;
	char** values;
} IniSection;

typedef struct
{
	int count;
	IniSection* sections;
} IniFile;

extern const NativeProp IniFileProp;
extern const NativeProp IniSectionProp;

bool ParseIni(IniFile* ini, const char* string);
void DestroyIni(IniFile* ini);