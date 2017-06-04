#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>

#include "iniparser.h"

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
		pos = strchr(pos + 1, '\n');	// Scan to next line

		if (pos)
			pos = strchr(pos + 1, '=');	// Scan to next equal pos
		
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

			if (lineEnd && lineEnd < equalPos)
				string = lineEnd + 1;	// now pointing to start of new line
			else
			{
				// We're already at the start of the line
			}
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

IniResult IniSet(IniFile* ini, const char* section, const char* key, const char* value)
{
	assert(key && value);

	if (!section)
		section = "";	// Global section has empty name

	for (int i = 0; i < ini->count; ++i)
	{
		if (strcmp(ini->sections[i].name, section) == 0)
		{
			IniSection* sec = &ini->sections[i];
			
			for (int i = 0; i < sec->count; ++i)
			{
				if (strcmp(sec->keys[i], key) == 0)
				{
					free(sec->values[i]);
					sec->values[i] = estrdup(value);
					
					return INI_SUCCESS;
				}
			}

			// No such key exists, so make it
			sec->keys = erealloc(sec->keys, sizeof(char*) * (sec->count + 1));
			sec->values = erealloc(sec->values, sizeof(char*) * (sec->count + 1));

			sec->keys[sec->count] = estrdup(key);
			sec->values[sec->count] = estrdup(value);

			sec->count += 1;
			return INI_NEW_KEY;
		}
	}

	// No such section exists, so create it
	ini->sections = erealloc(ini->sections, sizeof(IniSection) * (ini->count + 1));
	
	int pos = ini->count;

	if (strlen(section) == 0)
	{
		// Make sure empty section is the 0th section, shift the others up
		pos = 0;
		memmove(&ini->sections[1], &ini->sections[0], sizeof(IniSection) * ini->count);
	}

	IniSection* sec = &ini->sections[pos];
	sec->name = estrdup(section);

	ini->count += 1;

	sec->count = 1;
	
	sec->keys = emalloc(sizeof(char*));
	sec->values = emalloc(sizeof(char*));

	sec->keys[0] = estrdup(key);
	sec->values[0] = estrdup(value);

	return INI_NEW_SECTION;
}

IniResult IniDelete(IniFile* ini, const char* section, const char* key, bool removeSection)
{
	if (!section)
		section = "";

	int secIndex = -1;
	IniSection* sec = NULL;

	for (int i = 0; i < ini->count; ++i)
	{
		if (strcmp(ini->sections[i].name, section) == 0)
		{
			sec = &ini->sections[i];
			secIndex = i;
			break;
		}
	}

	if (!sec)
	{
		fprintf(stderr, "Unable to delete key from section '%s' in IniFile. No such section exists.\n", section);
		return INI_NO_SECTION;
	}

	if (key)
	{
		bool found = false;

		for (int i = 0; i < sec->count; ++i)
		{
			if (strcmp(sec->keys[i], key) == 0)
			{
				free(sec->keys[i]);
				free(sec->values[i]);

				// Shift key and value pointers back to fill in the hole
				memmove(&sec->keys[i], &sec->keys[i + 1], sizeof(char*) * (sec->count - i - 1));
				memmove(&sec->values[i], &sec->values[i + 1], sizeof(char*) * (sec->count - i - 1));
				
				sec->count -= 1;
				found = true;
				break;
			}
		}

		if (!found)
		{
			fprintf(stderr, "Unable to delete key '%s' from section '%s' in IniFile. No such key exists.\n", key, section);
			return INI_NO_KEY;
		}
	}
	
	if (!key || (removeSection && sec->count == 0))
	{
		free(sec->keys);
		free(sec->values);
		free(sec->name);

		// shift back to fill hole
		memmove(&ini->sections[secIndex], &ini->sections[secIndex + 1], sizeof(IniSection) * (ini->count - secIndex - 1));

		ini->count -= 1;
	}

	return INI_SUCCESS;
}

char* IniString(const IniFile* ini)
{
	int length = 0;

	// Compute size
	for (int i = 0; i < ini->count; ++i)
	{
		const IniSection* sec = &ini->sections[i];

		int nameLen = strlen(sec->name);

		// [name]\n
		if(nameLen > 0)
			length += 1 + nameLen + 2;

		for (int j = 0; j < sec->count; ++j)
		{
			// key=value\n
			length += strlen(sec->keys[j]) + 1 + strlen(sec->values[j]) + 1;
		}
	}

	char* str = emalloc(length + 1);

	// Write string
	int bytesWritten = 0;

	for (int i = 0; i < ini->count; ++i)
	{
		const IniSection* sec = &ini->sections[i];

		if(strlen(sec->name) > 0)
			bytesWritten += sprintf(str + bytesWritten, "[%s]\n", sec->name);

		for (int j = 0; j < sec->count; ++j)
			bytesWritten += sprintf(str + bytesWritten, "%s=%s\n", sec->keys[j], sec->values[j]);
	}

	assert(bytesWritten == length);

	return str;
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
