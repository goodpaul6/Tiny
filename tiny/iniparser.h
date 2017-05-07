#pragma once

#include <stdbool.h>

#include "tiny.h"

typedef enum
{
	INI_SUCCESS		= 1,
	INI_NEW_KEY		= 2,
	INI_NEW_SECTION	= 3,

	INI_NO_SECTION	= -1,
	INI_NO_KEY		= -2
} IniResult;

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
// Adds a key-value pair if it doesn't exist, and adds section if it doesn't exist
IniResult IniSet(IniFile* ini, const char* section, const char* key, const char* value);
// if removeSection is true, then it will remove the section if there are no
// keys left; if key is null, this is disregarded and the section is deleted
// unconditionally
IniResult IniDelete(IniFile* ini, const char* section, const char* key, bool removeSection);
void DestroyIni(IniFile* ini);