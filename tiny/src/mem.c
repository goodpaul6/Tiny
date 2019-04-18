#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

#if TINY_MEM_STACK_TRACE_COUNT > 0 && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#define MAX_SYMBOL_NAME	256
#endif

#include "t_mem.h"

#undef malloc
#undef realloc
#undef free

#ifdef TINY_MEM_CHECK
static tiny_allocation_record* record_list;
#endif

void tiny_init_mem(void)
{
#ifdef TINY_MEM_CHECK
	// TODO: Create pool for allocation records so keeping track of allocations
	// is faster
	record_list = NULL;
	
#if TINY_MEM_STACK_TRACE_COUNT > 0 && defined(_WIN32)
	HANDLE proc = GetCurrentProcess();
	
	SymSetOptions(SYMOPT_LOAD_LINES);
	SymInitialize(proc, NULL, TRUE);
#endif

#endif
}

#ifdef TINY_MEM_CHECK
const tiny_allocation_record* tiny_mem_get_allocations(void)
{
	return record_list;
}
#endif

void tiny_destroy_mem(void)
{
#ifdef TINY_MEM_CHECK
	tiny_allocation_record* node = record_list;
	
	while(node)
	{
		fprintf(stderr, "%s(%d): Leaked %zu bytes!\n", node->file, node->line, node->size);
		
#if TINY_MEM_STACK_TRACE_COUNT > 0 && defined(_WIN32)
		static char buffer[sizeof(SYMBOL_INFO) + MAX_SYMBOL_NAME];
		
		HANDLE proc = GetCurrentProcess();
		PSYMBOL_INFO p_symbol = (PSYMBOL_INFO)buffer;
		
		p_symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		p_symbol->MaxNameLen = MAX_SYMBOL_NAME;
		
		for(int i = 0; i < TINY_MEM_STACK_TRACE_COUNT; ++i)
		{
			if(SymFromAddr(proc, (DWORD64)node->callstack[i], NULL, p_symbol))
			{
				IMAGEHLP_LINE64 line;
				
				line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
				
				DWORD displacement;
				
				if(SymGetLineFromAddr64(proc, (DWORD64)node->callstack[i], &displacement, &line))
				{
					const char* filename = line.FileName;
					int num = (int)line.LineNumber;
									
					fprintf(stderr, "%i: %s(%i): %s - 0x%0llX\n", i, filename, num, p_symbol->Name, p_symbol->Address);
				}
				else
				{						
					DWORD error = GetLastError();
					if(error != 487)
						fprintf(stderr, "SymGetLineFromAddr64 failed with error code %d\n", error);
				}
			}
			else
			{
				DWORD error = GetLastError();
				if(error != 126)
					fprintf(stderr, "SymFromAddr failed with error code %d\n", error);
			}
		}
#endif
		
		tiny_allocation_record* next = node->next;
		free(node);
		node = next;
	}
	
#if TINY_MEM_STACK_TRACE_COUNT > 0 && defined(_WIN32)
	HANDLE proc = GetCurrentProcess();
	
	SymCleanup(proc);
#endif

#endif
}

void* _tiny_alloc(size_t size, const char* file, int line)
{
	void* mem = malloc(size);
	if(!mem)
	{
		fprintf(stderr, "%s(%d): Out of memory!\n", file, line);
		exit(1);
	}
	
#ifdef TINY_MEM_CHECK
	tiny_allocation_record* record = malloc(sizeof(tiny_allocation_record));
	assert(record);
	
	record->next = record_list;
	record_list = record;
	
	record->mem = mem;
	record->size = size;
	record->file = file;
	record->line = line;
	
#if TINY_MEM_STACK_TRACE_COUNT > 0

#ifdef _WIN32
	USHORT count = CaptureStackBackTrace(0, TINY_MEM_STACK_TRACE_COUNT, record->callstack, NULL);
#else
	memset(record->callstack, 0, sizeof(record->callstack);
#endif

#endif

#endif

	return mem;
}

static void print_stack_trace(void)
{
#if defined(_WIN32) && TINY_MEM_STACK_TRACE_COUNT > 0
	void* frames[64];

	HANDLE process = GetCurrentProcess();

	SymInitialize(process, NULL, TRUE);

	unsigned short numFrames = CaptureStackBackTrace(1, 64, frames, NULL);
	SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256, 1);
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (int i = 0; i < numFrames; ++i)
	{
		SymFromAddr(process, (DWORD64)(frames[i]), 0, symbol);

		IMAGEHLP_LINE64 line;

		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

		DWORD displacement;

		if (SymGetLineFromAddr64(process, (DWORD64)(frames[i]), &displacement, &line))
			fprintf(stderr, "%i: %s(%i): %s - 0x%0llX\n", i, line.FileName, (int)line.LineNumber, symbol->Name, symbol->Address);
		else
		{
			DWORD error = GetLastError();
			if(error != 487)
				fprintf(stderr, "SymGetLineFromAddr64 failed with error code %d\n", error);
		}
	}

	free(symbol);
#endif
}

void* _tiny_realloc(void* mem, size_t size, const char* file, int line)
{
	assert(size > 0);

	void* newMem = realloc(mem, size);
	if(!newMem)
	{
		fprintf(stderr, "%s(%d): Out of memory!\n", file, line);
		exit(1);
	}
	
#ifdef TINY_MEM_CHECK
	// Change the old memory record	
	tiny_allocation_record* record = record_list;
	
	while(record)
	{
		if(record->mem == mem)
		{
			record->mem = newMem;
			record->size = size;
			record->file = file;
			record->line = line;

#if TINY_MEM_STACK_TRACE_COUNT > 0
		
#ifdef _WIN32
			USHORT count = CaptureStackBackTrace(0, TINY_MEM_STACK_TRACE_COUNT, record->callstack, NULL);
#else
			memset(record->callstack, 0, sizeof(record->callstack);
#endif

#endif
			
			return newMem;
		}
		
		record = record->next;
	}

	if (!mem) {
		return _tiny_alloc(size, file, line);
	}
	
	fprintf(stderr, "%s(%d): Invalid realloc!\n", file, line);
	print_stack_trace();
	return NULL;
#else
	return newMem;
#endif
}

void _tiny_free(void* mem, const char* file, int line)
{
#ifdef TINY_MEM_CHECK
	if(!mem) return;

	tiny_allocation_record** record = &record_list;
	
	while(*record)
	{
		if(mem == (*record)->mem)
		{
			tiny_allocation_record* removed = *record;
			*record = (*record)->next;
			free(mem);
			free(removed);
			
			return;
		}
		else
			record = &(*record)->next;
	}
	
	fprintf(stderr, "%s(%d): Invalid free!\n", file, line);
	print_stack_trace();
#else
	free(mem);
#endif
}
