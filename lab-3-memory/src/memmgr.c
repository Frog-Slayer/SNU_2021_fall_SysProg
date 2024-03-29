//--------------------------------------------------------------------------------------------------
// System Programming                       Memory Lab                                   Fall 2021
//
/// @file
/// @brief dynamic memory manager
/// @author <Park YeongSeo>
/// @studid <2016-13006>
//--------------------------------------------------------------------------------------------------


// Dynamic memory manager
// ======================
// This module implements a custom dynamic memory manager.
//
// Heap organization:
// ------------------
// The data segment for the heap is provided by the dataseg module. A 'word' in the heap is
// eight bytes.
//
// Implicit free list:
// -------------------
// - minimal block size: 32 bytes (header +footer + 2 data words)
// - h,f: header/footer of free block
// - H,F: header/footer of allocated block
//
// - state after initialization
//
//         initial sentinel half-block                  end sentinel half-block
//                   |                                             |
//   ds_heap_start   |   heap_start                         heap_end       ds_heap_brk
//               |   |   |                                         |       |
//               v   v   v                                         v       v
//               +---+---+-----------------------------------------+---+---+
//               |???| F | h :                                 : f | H |???|
//               +---+---+-----------------------------------------+---+---+
//                       ^                                         ^
//                       |                                         |
//               32-byte aligned                           32-byte aligned
//
// - allocation policies: first, next, best fit
// - block splitting: always at 32-byte boundaries
// - immediate coalescing upon free
//


#include <assert.h>
#include <error.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dataseg.h"
#include "memmgr.h"

void mm_check(void);

/// @name global variables
/// @{
static void *ds_heap_start = NULL;                     ///< physical start of data segment
static void *ds_heap_brk   = NULL;                     ///< physical end of data segment
static void *heap_start    = NULL;                     ///< logical start of heap
static void *heap_end      = NULL;                     ///< logical end of heap
static int  PAGESIZE       = 0;                        ///< memory system page size
static void *(*get_free_block)(size_t) = NULL;         ///< get free block for selected allocation policy
static int  mm_initialized = 0;                        ///< initialized flag (yes: 1, otherwise 0)
static int  mm_loglevel    = 0;                        ///< log level (0: off; 1: info; 2: verbose)
static void *nf_ptr		   = NULL;					   ///< a pointer to save the lately searched address, for next-fit allocation policy
/// @}

/// @name Macro definitions
/// @{
#define MAX(a, b)          ((a) > (b) ? (a) : (b))     ///< MAX function

#define TYPE               unsigned long               ///< word type of heap
#define TYPE_SIZE          sizeof(TYPE)                ///< size of word type

#define ALLOC              1                           ///< block allocated flag
#define FREE               0                           ///< block free flag
#define STATUS_MASK        ((TYPE)(0x7))               ///< mask to retrieve flagsfrom header/footer
#define SIZE_MASK          (~STATUS_MASK)              ///< mask to retrieve size from header/footer

#define CHUNKSIZE          (1*(1 << 12))               ///< size by which heap is extended

#define BS                 32                          ///< minimal block size. Must be a power of 2
#define BS_MASK            (~(BS-1))                   ///< alignment mask

#define WORD(p)            ((TYPE)(p))                 ///< convert pointer to TYPE
#define PTR(w)             ((void*)(w))                ///< convert TYPE to void*

#define PREV_PTR(p)        ((p)-TYPE_SIZE)             ///< get pointer to word preceeding p

#define PACK(size,status)  ((size) | (status))         ///< pack size & status into boundary tag
#define SIZE(v)            (v & SIZE_MASK)             ///< extract size from boundary tag
#define STATUS(v)          (v & STATUS_MASK)           ///< extract status from boundary tag

#define GET(p)             (*(TYPE*)(p))               ///< read word at *p
#define GET_SIZE(p)        (SIZE(GET(p)))              ///< extract size from header/footer
#define GET_STATUS(p)      (STATUS(GET(p)))            ///< extract status from header/footer

// TODO add more macros as needed
#define NEXT_PTR(p)		   ((p)+TYPE_SIZE)			   ///< get pointer to word following p

#define NEXT_BLKP(p)	   ((p) + GET_SIZE(HDRP(p)))   ///< address of next block
#define PREV_BLKP(p)	   ((p) - GET_SIZE(PREV_PTR(HDRP(p)))) ///< address of prev block

#define PUT(p, val)		   (*(TYPE*)(p) = (val))	   ///< write word at *p

#define HEAP_SIZE	   	   (heap_end - heap_start)	   ///< the size of heap	   

#define HDRP(bp)		   (PREV_PTR(bp))			   ///< find address of the header, given block pointer bp						
#define FTRP(bp)		   (PREV_PTR(HDRP(bp) + GET_SIZE(HDRP(bp)))) ///< find address of the footer, given block pointer bp


/// @brief print a log message if level <= mm_loglevel. The variadic argument is a printf format
///        string followed by its parametrs
#ifdef DEBUG
  #define LOG(level, ...) mm_log(level, __VA_ARGS__)

/// @brief print a log message. Do not call directly; use LOG() instead
/// @param level log level of message.
/// @param ... variadic parameters for vprintf function (format string with optional parameters)
static void mm_log(int level, ...)
{
  if (level > mm_loglevel) return;

  va_list va;
  va_start(va, level);
  const char *fmt = va_arg(va, const char*);

  if (fmt != NULL) vfprintf(stdout, fmt, va);

  va_end(va);

  fprintf(stdout, "\n");
}

#else
  #define LOG(level, ...)
#endif

/// @}


/// @name Program termination facilities
/// @{

/// @brief print error message and terminate process. The variadic argument is a printf format
///        string followed by its parameters
#define PANIC(...) mm_panic(__func__, __VA_ARGS__)

/// @brief print error message and terminate process. Do not call directly, Use PANIC() instead.
/// @param func function name
/// @param ... variadic parameters for vprintf function (format string with optional parameters)
static void mm_panic(const char *func, ...)
{
  va_list va;
  va_start(va, func);
  const char *fmt = va_arg(va, const char*);

  fprintf(stderr, "PANIC in %s%s", func, fmt ? ": " : ".");
  if (fmt != NULL) vfprintf(stderr, fmt, va);

  va_end(va);

  fprintf(stderr, "\n");

  exit(EXIT_FAILURE);
}
/// @}


static void* ff_get_free_block(size_t);
static void* nf_get_free_block(size_t);
static void* bf_get_free_block(size_t);

void mm_init(AllocationPolicy ap)
{
  LOG(1, "mm_init()");

  //
  // set allocation policy
  //
  char *apstr;
  switch (ap) {
    case ap_FirstFit: get_free_block = ff_get_free_block; apstr = "first fit"; break;
    case ap_NextFit:  get_free_block = nf_get_free_block; apstr = "next fit";  break;
    case ap_BestFit:  get_free_block = bf_get_free_block; apstr = "best fit";  break;
    default: PANIC("Invalid allocation policy.");
  }
  LOG(2, "  allocation policy       %s\n", apstr);

  //
  // retrieve heap status and perform a few initial sanity checks
  //
  ds_heap_stat(&ds_heap_start, &ds_heap_brk, NULL);
  PAGESIZE = ds_getpagesize();

  LOG(2, "  ds_heap_start:          %p\n"
         "  ds_heap_brk:            %p\n"
         "  PAGESIZE:               %d\n",
         ds_heap_start, ds_heap_brk, PAGESIZE);

  if (ds_heap_start == NULL) PANIC("Data segment not initialized.");
  if (ds_heap_start != ds_heap_brk) PANIC("Heap not clean.");
  if (PAGESIZE == 0) PANIC("Reported pagesize == 0.");

  //
  // initialize heap
  //
  
  // 1. make some memory
  ds_sbrk(CHUNKSIZE);
  ds_heap_brk = ds_sbrk(0);
  if (ds_heap_brk == (void*)-1) PANIC("Cannot extend heap");


  // 2. set sentinels
  //	- set start & end of heap
  heap_start = (TYPE*)(((TYPE)ds_heap_start + BS) / BS * BS);
  heap_end = (TYPE*) (((TYPE)ds_heap_brk -1) / BS * BS);

  //	- set initial/end sentinels 
  PUT(PREV_PTR(heap_start),	PACK(0, ALLOC));
  PUT(heap_end, 			PACK(0, ALLOC));


  // 3. create initial free block
  //	- set header 
  PUT(heap_start, 			PACK(HEAP_SIZE, FREE));
  //	- set footer
  PUT(PREV_PTR(heap_end), 	PACK(HEAP_SIZE, FREE));
  //
  // heap is initialized
  //
  mm_initialized = 1;
}


void* mm_malloc(size_t size)
{
  LOG(1, "mm_malloc(0x%lx)", size);
  assert(mm_initialized);

  size_t asize; ///< adjusted block size
  size_t xsize; ///< amount to extend heap
  void* bp;

  // 1. adjust block size
  //	- if size == 0, we won't allocate
  if (size == 0) return NULL;
  //	- adjust block size to include overhead & alignment reqs
  asize = ((size + 2 * TYPE_SIZE - 1) / BS + 1) * BS;
  LOG(0, "size : %d --> adjusted size: %d", size, asize);
  
  // 2. find a fit free block according to policy
  //	- if there's a fit free block, split if possible. Set tags' size and their flags, and return
  if ((bp = get_free_block(asize)) != NULL) {
	if (GET_SIZE(HDRP(bp)) > asize) {
		PUT(HDRP(bp) + asize, 		PACK(GET_SIZE(HDRP(bp)) - asize, FREE));
		PUT(FTRP(bp)		,		PACK(GET_SIZE(HDRP(bp)) - asize, FREE));
		
		PUT(HDRP(bp)		,		PACK(asize, ALLOC));
		PUT(FTRP(bp)		, 		PACK(asize, ALLOC));
	}
	else {
		PUT(HDRP(bp)		,		PACK(asize, ALLOC));
		PUT(FTRP(bp)		,		PACK(asize, ALLOC));
	}
	return bp;
  }

  // 3. if we cannot find any fit free block, then get more memory and allocate.
  //	- get more memory
  xsize = MAX(asize, CHUNKSIZE);
  bp = ds_sbrk(xsize);
  ds_heap_brk = ds_sbrk(0);
  if (ds_heap_brk == (void*)-1) PANIC("Cannot extend heap");
  
  heap_end = (TYPE*)(((TYPE)ds_heap_brk -1) / BS * BS);
  PUT(heap_end, PACK(0, ALLOC));

  // 	- allocate
  PUT(HDRP(bp), PACK(xsize, ALLOC));
  PUT(FTRP(bp), PACK(xsize, ALLOC));
  return bp;
}

void* mm_calloc(size_t nmemb, size_t size)
{
  LOG(1, "mm_calloc(0x%lx, 0x%lx)", nmemb, size);

  assert(mm_initialized);

  //
  // calloc is simply malloc() followed by memset()
  //
  void *payload = mm_malloc(nmemb * size);

  if (payload != NULL) memset(payload, 0, nmemb * size);

  return payload;
}

void* mm_realloc(void *ptr, size_t size)
{
  LOG(1, "mm_realloc(%p, 0x%lx)", ptr, size);

  assert(mm_initialized);

  //
  // TODO (optional)
  //

  return NULL;
}

void mm_free(void *ptr)
{
  LOG(1, "mm_free(%p)", ptr);

  assert(mm_initialized);

  // 1. if ptr points freed memory block, an error is printed
  if (!GET_STATUS(HDRP(ptr))) PANIC("You're trying to free already free block."); 
  
  size_t size = GET_SIZE(HDRP(ptr));

  // 2. Set flags of tags FREE
  PUT(HDRP(ptr), PACK(size, FREE));
  PUT(FTRP(ptr), PACK(size, FREE));

  // 3. coalescing
  TYPE* prev_bp = (TYPE*)PREV_BLKP(ptr);
  TYPE* next_bp = (TYPE*)NEXT_BLKP(ptr);
  TYPE* prev_hdr = (TYPE*)HDRP(prev_bp);
  TYPE* next_hdr = (TYPE*)HDRP(next_bp);


  size_t prev_alloc = GET_STATUS(prev_hdr);
  size_t next_alloc = GET_STATUS(next_hdr);
  if (prev_alloc && next_alloc) {
	  return;
  }
  else if (prev_alloc && !next_alloc){
	  size += GET_SIZE(next_hdr);
	  PUT(HDRP(ptr), PACK(size, FREE));
	  PUT(FTRP(next_bp), PACK(size, FREE));
  }
  else if (!prev_alloc && next_alloc){
	  size += GET_SIZE(prev_hdr);
	  PUT(prev_hdr, PACK(size, FREE));
	  PUT(FTRP(ptr), PACK(size,FREE));
  }
  else {
	 size += (GET_SIZE(prev_hdr) + GET_SIZE(next_hdr));
	 PUT(prev_hdr, PACK(size, FREE));
	 PUT(FTRP(next_bp), PACK(size, FREE));
  }

}

/// @name block allocation policites
/// @{

/// @brief find and return a free block of at least @a size bytes (first fit)
/// @param size size of block (including header & footer tags), in bytes
/// @retval void* pointer to header of large enough free block
/// @retval NULL if no free block of the requested size is available
static void* ff_get_free_block(size_t size)
{
  LOG(1, "ff_get_free_block(1x%lx (%lu))", size, size);

  assert(mm_initialized);

  void* hdrp = heap_start;

  // 1. find the first fit block using header
  while ((hdrp < heap_end) && ((size > GET_SIZE(hdrp)) || (GET_STATUS(hdrp) == ALLOC))) {
	hdrp += GET_SIZE(hdrp);
  }

  // 2. if there's fit block, return its block pointer
  if (hdrp < heap_end) return NEXT_PTR(hdrp);

  // 3. if there's no fit block, then return NULL
  return NULL;
}

/// @brief find and return a free block of at least @a size bytes (next fit)
/// @param size size of block (including header & footer tags), in bytes
/// @retval void* pointer to header of large enough free block
/// @retval NULL if no free block of the requested size is avilable
static void* nf_get_free_block(size_t size)
{
  LOG(1, "nf_get_free_block(0x%x (%lu))", size, size);

  assert(mm_initialized);

  // 1. if nf_ptr is NULL, set it start of the heap
  if (nf_ptr == NULL) nf_ptr = heap_start;

  // 2. search from the lately searched address to the end
  while ((nf_ptr < heap_end) && ((size > GET_SIZE(nf_ptr)) || (GET_STATUS(nf_ptr) == ALLOC))){
	nf_ptr += GET_SIZE(nf_ptr);
  }

  // 3. if there's a fit block found, return its block pointer
  if (nf_ptr < heap_end) return NEXT_PTR(nf_ptr);
  
  // 4. otherwise, set nf_ptr NULL and return it
  return nf_ptr = NULL;
}

/// @brief find and return a free block of at least @a size bytes (best fit)
/// @param size size of block (including header & footer tags), in bytes
/// @retval void* pointer to header of large enough free block
/// @retval NULL if no free block of the requested size is avilable
static void* bf_get_free_block(size_t size)
{
  LOG(1, "bf_get_free_block(0x%lx (%lu))", size, size);

  assert(mm_initialized);

  void *hdrp = heap_start;

  size_t minSize = HEAP_SIZE;	///< save the minimum size of the block searched 
  void *bf_ptr = NULL;			///< a pointer that points block with minimum size

  // 1. search all the blocks from the start to the end, and find the best-fit block
  while (hdrp < heap_end){
	if ((size <= GET_SIZE(hdrp)) && (GET_STATUS(hdrp) == FREE)) {
		if (GET_SIZE(hdrp) < minSize) {
			bf_ptr = hdrp;
			minSize = GET_SIZE(hdrp);
		}
	}
	hdrp += GET_SIZE(hdrp);
  }
  

  if (bf_ptr != NULL) return NEXT_PTR(bf_ptr);
  
  return NULL;

}

/// @}

void mm_setloglevel(int level)
{
  mm_loglevel = level;
}


void mm_check(void)
{
  assert(mm_initialized);

  void *p;
  char *apstr;
  if (get_free_block == ff_get_free_block) apstr = "first fit";
  else if (get_free_block == nf_get_free_block) apstr = "next fit";
  else if (get_free_block == bf_get_free_block) apstr = "best fit";
  else apstr = "invalid";

  LOG(2, "  allocation policy    %s\n", apstr);
  printf("\n----------------------------------------- mm_check ----------------------------------------------\n");
  printf("  ds_heap_start:          %p\n", ds_heap_start);
  printf("  ds_heap_brk:            %p\n", ds_heap_brk);
  printf("  heap_start:             %p\n", heap_start);
  printf("  heap_end:               %p\n", heap_end);
  printf("  allocation policy:      %s\n", apstr);
  //printf("  next_block:             %p\n", next_block);   // this will be needed for the next fit policy

  printf("\n");
  p = PREV_PTR(heap_start);
  printf("  initial sentinel:       %p: size: %6lx (%7ld), status: %s\n",
         p, GET_SIZE(p), GET_SIZE(p), GET_STATUS(p) == ALLOC ? "allocated" : "free");
  p = heap_end;
  printf("  end sentinel:           %p: size: %6lx (%7ld), status: %s\n",
         p, GET_SIZE(p), GET_SIZE(p), GET_STATUS(p) == ALLOC ? "allocated" : "free");
  printf("\n");
  printf("  blocks:\n");

  long errors = 0;
  p = heap_start;
  while (p < heap_end) {
    TYPE hdr = GET(p);
    TYPE size = SIZE(hdr);
    TYPE status = STATUS(hdr);
    printf("    %p: size: %6lx (%7ld), status: %s\n", 
           p, size, size, status == ALLOC ? "allocated" : "free");

    void *fp = p + size - TYPE_SIZE;
    TYPE ftr = GET(fp);
    TYPE fsize = SIZE(ftr);
    TYPE fstatus = STATUS(ftr);

    if ((size != fsize) || (status != fstatus)) {
      errors++;
      printf("    --> ERROR: footer at %p with different properties: size: %lx, status: %lx\n", 
             fp, fsize, fstatus);
    }

    p = p + size;
    if (size == 0) {
      printf("    WARNING: size 0 detected, aborting traversal.\n");
      break;
    }
  }

  printf("\n");
  if ((p == heap_end) && (errors == 0)) printf("  Block structure coherent.\n");
  printf("-------------------------------------------------------------------------------------------------\n");
}
