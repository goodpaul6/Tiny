#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>

#include "iniparser.h"

static void IniFree(void* pi)
{
	IniFile* ini = pi;

	DestroyIni(ini);

	free(ini);
}

const NativeProp IniFileProp = {
	"ini_file",
	NULL,
	IniFree,
	NULL
};

const NativeProp IniSectionProp = {
	"ini_section",
	NULL,
	NULL,
	NULL,
};

static int CountSections(const char* string)
{
	int count = 0;
	const char* pos = strchr(string, '[');

	while (pos)
	{
		++count;
		pos = strchr(pos + 1, '[');
	}

	return count;
}

// Counts the number of key value pairs until
// a '[' is encountered
static int CountMembers(const char* string)
{
	int count = 0;

	const char* pos = strchr(string, '=');
	const char* nextSection = strchr(string, '[');

	while (pos)
	{
		++count;
		pos = strchr(pos + 1, '=');
		
		if (nextSection && pos >= nextSection)
			break;
	}

	return count;
}

bool ParseIni(IniFile* ini, const char* string)
{
	bool hasGlobal = false;

	const char* start = strchr(string, '[');
	const char* equal = strchr(string, '=');

	if (equal < start)
		hasGlobal = true;

	ini->count = CountSections(string) + (hasGlobal ? 1 : 0);
	ini->sections = emalloc(sizeof(IniSection) * ini->count);

	for (int i = 0; i < ini->count; ++i)
	{
		if (hasGlobal && i == 0)
		{
			// Yes, it's the empty string
			ini->sections[i].name = estrdup("");

			// Look for a lineEnd which is just before an equal sign (i.e skip empty lines)
			char* lineEnd = strchr(string, '\n');
			char* equalPos = strchr(string, '=');

			// since it hasGlobal, this equal sign must exist
			assert(equalPos);

			char* pos = lineEnd;

			while (pos < equalPos)
			{
				pos = strchr(pos + 1, '\n');
				if (pos && pos < equalPos)
					lineEnd = pos;
			}

			assert(lineEnd && lineEnd < equalPos);
			string = lineEnd + 1;	// now pointing to start of new line
		}
		else
		{
			const char* sectionStart = strchr(string, '[');

			assert(sectionStart);

			const char* sectionNameEnd = strchr(string, ']');

			int nameLen = (sectionNameEnd - 1) - sectionStart;

			if (nameLen <= 0)
			{
				fprintf(stderr, "Empty section name is not allowed.\n");
				return false;
			}

			char* name = emalloc(nameLen + 1);

			strncpy(name, sectionStart + 1, nameLen);
			name[nameLen] = '\0';

			for (int j = 0; j < i; ++j)
			{
				if (strcmp(ini->sections[j].name, name) == 0)
				{
					fprintf(stderr, "Multiple ini sections with the same name '%s'.\n", name);
					return false;
				}
			}

			ini->sections[i].name = name;

			string = strchr(sectionNameEnd + 1, '\n');

			// skip new line char, now string points to the start of the new line
			string += 1;
		
			// skip empty lines
			if (*string)
			{
				char* sectionStart = strchr(string, '[');

				char* nextLineEnd = strchr(string, '\n');
				char* equalPos = strchr(string, '=');

				if (!sectionStart || equalPos < sectionStart)
				{
					while (nextLineEnd && nextLineEnd < equalPos)
					{
						string = nextLineEnd + 1;
						nextLineEnd = strchr(string, '\n');
					}
				}
			}
		}

		if (!string)
		{
			ini->sections[i].count = 0;
			continue;
		}

		int count = CountMembers(string);

		ini->sections[i].count = count;
		
		ini->sections[i].keys = emalloc(sizeof(char*) * count);
		ini->sections[i].values = emalloc(sizeof(char*) * count);

		const char* equalPos = strchr(string, '=');
		
		for(int index = 0; index < count; ++index)
		{
			const char* lineEnd = strchr(string, '\n');

			if (!lineEnd)
			{
				lineEnd = strchr(string, '\0');
				assert(lineEnd);
			}

			int keyLen = equalPos - string;
			char* key = emalloc(keyLen + 1);

			strncpy(key, string, keyLen);
			key[keyLen] = '\0';

			int valueLen = lineEnd - (equalPos + 1);

			// get rid of carriage return bullshit
			if (*(lineEnd - 1) == '\r')
				valueLen -= 1;

			char* value = emalloc(valueLen + 1);

			strncpy(value, equalPos + 1, valueLen);
			value[valueLen] = '\0';

			ini->sections[i].keys[index] = key;
			ini->sections[i].values[index] = value;

			// On to the next line (skipping empty lines in between), reset equal pos
			if (*lineEnd)
			{
				string = lineEnd + 1;
				equalPos = strchr(string, '=');

				char* sectionStart = strchr(string, '[');
				
				// Only skip to the next significant line if it's within the current section
				if (!sectionStart || equalPos < sectionStart)
				{
					char* nextLineEnd = strchr(string, '\n');

					while (nextLineEnd && nextLineEnd < equalPos)
					{
						string = nextLineEnd + 1;
						nextLineEnd = strchr(string, '\n');
					}
				}
			}
			else		// String is done
				break;
		}
	}

	return true;
}

void DestroyIni(IniFile* ini)
{
	for (int i = 0; i < ini->count; ++i)
	{
		IniSection* section = &ini->sections[i];
		
		free(section->name);

		for (int i = 0; i < section->count; ++i)
		{
			free(section->keys[i]);
			free(section->values[i]);
		}

		free(section->keys);
		free(section->values);
	}

	free(ini->sections);
}