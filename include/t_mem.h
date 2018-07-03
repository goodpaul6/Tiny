#pragma once

#include <stddef.h>

#ifdef TINY_MEM_CHECK

#ifndef TINY_MEM_ERR_STREAM 
#define TINY_MEM_ERR_STREAM stderr
#endif

typedef struct tiny_allocation_record
{
	struct tiny_allocation_record* next;
	
	void* mem;
	size_t size;
	const char* file;
	int line;
	
#if TINY_MEM_STACK_TRACE_COUNT > 0
	void* callstack[TINY_MEM_STACK_TRACE_COUNT];
#endif
} tiny_allocation_record;

#endif

void tiny_init_mem(void);

#ifdef TINY_MEM_CHECK
const tiny_allocation_record* tiny_mem_get_allocations(void);
#endif

void* _tiny_alloc(size_t size, const char* file, int line);
#define tiny_alloc(size) _tiny_alloc((size), __FILE__, __LINE__)

void* _tiny_realloc(void* mem, size_t size, const char* file, int line);
#define tiny_realloc(mem, size) _tiny_realloc((mem), (size), __FILE__, __LINE__)

void _tiny_free(void* mem, const char* file, int line);
#define tiny_free(mem) _tiny_free((mem), __FILE__, __LINE__)

void tiny_destroy_mem(void);
