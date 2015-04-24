/*
 * Copyright 2004-2015 Cray Inc.
 * Other additional copyright holders may be indicated within.
 * 
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 * 
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// Shared code for different mem implementations in mem-*/chpl_*_mem.c
//
#include "chplrt.h"

#include "chpl-mem.h"
#include "chpltypes.h"
#include "error.h"

// This file needs to be able to use real allocator names
#undef malloc
#undef calloc
#undef realloc
#undef free

// This file always declares this function at least.
void chpl_mem_replace_malloc_if_needed(void);

#ifndef CHPL_USING_CSTDLIB_MALLOC
// Don't declare anything unless we're not using C allocator.


#ifndef CHPL_USING_CSTDLIB_MALLOC
#ifdef __GLIBC__
#define USE_GLIBC_MALLOC_HOOKS
#endif
#endif

#ifdef __GLIBC__
#include <malloc.h> // for memalign
#endif

#define CHPL_REPLACE_MALLOC 1

#ifdef USE_GLIBC_MALLOC_HOOKS
#define TRACK_SYSTEM_ALLOCATION
#endif
#ifdef CHPL_REPLACE_MALLOC
#define TRACK_SYSTEM_ALLOCATION
#define TRACK_SYSTEM_ALLOCATION_NOARG
#endif

#ifdef TRACK_SYSTEM_ALLOCATION

struct system_allocated_ptr {
  struct system_allocated_ptr* next;
  void* ptr;
  size_t len;
};

static struct system_allocated_ptr* system_allocated_head;

#ifdef USE_GLIBC_MALLOC_HOOKS
// This version is for glibc malloc hooks that take an extra argument
static void track_system_allocated_arg(
  void* ptr,
  size_t len,
  void * (*original_malloc) (size_t, const void *),
  const void * arg)
{
  struct system_allocated_ptr* cur;
  cur = (struct system_allocated_ptr*)
            original_malloc(sizeof(struct system_allocated_ptr), arg);
  cur->next = system_allocated_head;
  cur->ptr = ptr;
  cur->len = len;
  system_allocated_head = cur;
}
#endif

#ifdef TRACK_SYSTEM_ALLOCATION_NOARG
// This version is for linker malloc replacements
static void track_system_allocated(
  void* ptr,
  size_t len,
  void * (*real_malloc) (size_t) )
{
  struct system_allocated_ptr* cur;
  cur = (struct system_allocated_ptr*)
            real_malloc(sizeof(struct system_allocated_ptr));
  cur->next = system_allocated_head;
  cur->ptr = ptr;
  cur->len = len;
  system_allocated_head = cur;
}
#endif

static int is_system_allocated(void* ptr_in)
{
  unsigned char* ptr = (unsigned char*) ptr_in;
  struct system_allocated_ptr* cur;
  for( cur = system_allocated_head;
       cur;
       cur = cur->next ) {
    unsigned char* start = cur->ptr;
    unsigned char* end = start + cur->len;
    if( start <= ptr && ptr < end ) return 1;
  }
  return 0;
}

#endif

#ifdef CHPL_REPLACE_MALLOC
void* __libc_calloc(size_t n, size_t size);
void* calloc(size_t n, size_t size);

void* __libc_malloc(size_t size);
void* malloc(size_t size);

void* __libc_memalign(size_t alignment, size_t size);
void* memalign(size_t alignment, size_t size);

void* __libc_realloc(void* ptr, size_t size);
void* realloc(void* ptr, size_t size);

void __libc_free(void* ptr);
void free(void* ptr);

int posix_memalign(void **memptr, size_t alignment, size_t size);

void* __libc_valloc(size_t size);
void* valloc(size_t size);

void* __libc_pvalloc(size_t size);
void* pvalloc(size_t size);

void* calloc(size_t n, size_t size)
{
  void* ret;
  if( !chpl_mem_inited() ) {
    ret = __libc_calloc(n, size);
    printf("in early calloc %p = system calloc(%#x)\n",
           ret, (int) (n*size));
    track_system_allocated(ret, n*size, __libc_malloc);
    return ret;
  }
  printf("in calloc\n");
  ret = chpl_calloc(n, size);
  printf("%p = chpl_calloc(%#x)\n", ret, (int) (n*size));
  return ret;
}

void* malloc(size_t size)
{
  void* ret;
  if( !chpl_mem_inited() ) {
    ret = __libc_malloc(size);
    printf("in early malloc %p = system malloc(%#x)\n", ret, (int) size);
    track_system_allocated(ret, size, __libc_malloc);
    return ret;
  }
  printf("in malloc\n");
  ret = chpl_malloc(size);
  printf("%p = chpl_malloc(%#x)\n", ret, (int) size);
  return ret;
}

void* memalign(size_t alignment, size_t size)
{
  if( !chpl_mem_inited() ) {
    void* ret = __libc_memalign(alignment, size);
    printf("in early memalign %p = system memalign(%#x)\n",
           ret, (int) size);
    track_system_allocated(ret, size, __libc_malloc);
    return ret;
  }
  printf("in memalign\n");
  return chpl_memalign(alignment, size);
}

void* realloc(void* ptr, size_t size)
{
  if( !chpl_mem_inited() ) {
    void* ret = __libc_realloc(ptr, size);
    printf("in early realloc %p = system realloc(%#x)\n",
           ret, (int) size);
    track_system_allocated(ret, size, __libc_malloc);
    return ret;
  }
  printf("in realloc\n");
  return chpl_realloc(ptr, size);
}

void free(void* ptr)
{
  if( ! ptr ) return;
  printf("in free(%p)\n", ptr);
  // check to see if we're freeing a pointer that was allocated
  // before the our allocator came up.
  if( !chpl_mem_inited() || is_system_allocated(ptr) ) {
    printf("calling system free\n");
    __libc_free(ptr);
    return;
  }

  printf("calling chpl_free\n");
  chpl_free(ptr);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
  int ret;
  if( !chpl_mem_inited() ) {
    *memptr = NULL;
    ret = chpl_posix_memalign_check_valid(alignment);
    if( ret ) return ret;
    *memptr = memalign(alignment, size);
    if( ! *memptr ) return ENOMEM;
    printf("in early posix_memalign %p = system posix_memalign(%#x)\n",
           *memptr, (int) size);
    track_system_allocated(*memptr, size, __libc_malloc);
    return 0;
  }
  printf("in posix_memalign\n");
  ret = chpl_posix_memalign(memptr, alignment, size);
  printf("%p = chpl_posix_memalign(%#x, %#x) returned %i\n",
         *memptr, (int) alignment, (int) size, ret);
  return ret;
}

void* valloc(size_t size)
{
  void* ret;
  if( !chpl_mem_inited() ) {
    ret = __libc_valloc(size);
    printf("in early valloc %p = system valloc(%#x)\n", ret, (int) size);
    track_system_allocated(ret, size, __libc_malloc);
    return ret;
  }
  printf("in valloc\n");
  ret = chpl_valloc(size);
  printf("%p = chpl_valloc(%#x)\n", ret, (int) size);
  return ret;
}

void* pvalloc(size_t size)
{
  if( !chpl_mem_inited() ) {
    void* ret = __libc_pvalloc(size);
    printf("in early pvalloc %p = system pvalloc(%#x)\n",
           ret, (int) size);
    track_system_allocated(ret, size, __libc_malloc);
    return ret;
  }
  printf("in pvalloc\n");
  return chpl_pvalloc(size);
}
#endif

#ifdef USE_GLIBC_MALLOC_HOOKS

// GLIBC requests that we define __malloc_initialize_hook like this
void (* __malloc_initialize_hook) (void) = chpl_mem_replace_malloc_if_needed;

static void * (*original_malloc)  (size_t, const void *);
static void * (*original_memalign)(size_t, size_t, const void *);
static void * (*original_realloc) (void *, size_t, const void *);
static void   (*original_free)    (void *, const void *);

static
void* chpl_malloc_hook(size_t size, const void* arg)
{
  if( !chpl_mem_inited() ) {
    void* ret = original_malloc(size, arg);
    track_system_allocated_arg(ret, size, original_malloc, arg);
    return ret;
  }
  printf("in chpl_malloc_hook\n");
  return chpl_malloc(size);
}

static
void* chpl_memalign_hook(size_t alignment, size_t size, const void* arg)
{
  if( !chpl_mem_inited() ) {
    void* ret = original_memalign(alignment, size, arg);
    track_system_allocated_arg(ret, size, original_malloc, arg);
    return ret;
  }
  printf("in chpl_memalign_hook\n");
  return chpl_memalign(alignment, size);
}

static
void* chpl_realloc_hook(void* ptr, size_t size, const void* arg)
{
  if( !chpl_mem_inited() ) {
    void* ret = original_realloc(ptr, size, arg);
    track_system_allocated_arg(ret, size, original_malloc, arg);
    return ret;
  }
  printf("in chpl_realloc_hook\n");
  return chpl_realloc(ptr,size);
}

static
void chpl_free_hook(void* ptr, const void* arg) {
  if( ! ptr ) return;
  if( !chpl_mem_inited() || is_system_allocated(ptr) ) {
    original_free(ptr, arg);
    return;
  }
  printf("in chpl_free_hook\n");
  chpl_free(ptr);
}

#endif


#endif



void chpl_mem_replace_malloc_if_needed(void) {
#ifdef CHPL_USING_CSTDLIB_MALLOC
  // do nothing, we're already using the C stdlib allocator.
#else
  // try using malloc hooks for glibc
  static int hooksInstalled = 0;
  if( hooksInstalled ) return;
  hooksInstalled = 1;

  printf("in chpl_mem_replace_malloc_if_needed\n");

#ifdef USE_GLIBC_MALLOC_HOOKS
  // glibc wants a memalign call
  // glibc: void *memalign(size_t alignment, size_t size);
  // dlmalloc: dlmemalign
  // tcmalloc: tc_memalign
  original_malloc = __malloc_hook;
  original_memalign = __memalign_hook;
  original_realloc = __realloc_hook;
  original_free = __free_hook;

  __malloc_hook = chpl_malloc_hook;
  __memalign_hook = chpl_memalign_hook;
  __realloc_hook = chpl_realloc_hook;
  __free_hook = chpl_free_hook;

#endif

  // We could do this too for Mac OS X if it mattered.
  // - include malloc/malloc.h
  // - replace functions in malloc_default_zone returned malloc_zone_t*
  // functions needed include a "size" function and a "valloc" function:
  // BSD: malloc_size
  // glibc: malloc_usable_size
  // dlmalloc: dlmalloc_usable_size
  // tcmalloc: tc_malloc_size
#endif
}

