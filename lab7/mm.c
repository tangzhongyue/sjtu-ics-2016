/*
 * mm.c
 * 
 * In this approach, an explicit list is used to store free blocks.
 * A free block consists of :| header(contains size and alloc info)  | address of previous free block | address of next free block|           empty         | footer (same as header)  | 
 *                           |              4 bytes                  |             4 bytes            |           4 bytes         |    aligned to 8 bytes   |          4 bytes         |
 * An allocated block consists of :| header(contains size and alloc info)  |   data and padding  | header(contains size and alloc info)  |
 *                                 |                4 bytes                |  aligned to 8 bytes |                 4 bytes               |
 * The free blocks in the free block list are linked by the value of address, from small to big
 * Functions insert_block and remove_block are used to insert new free block into the list or remove allocated block from the list
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "515030910312",
    /* First member's full name */
    "唐仲乐",
    /* First member's email address */
    "tangzhongyue@sjtu.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  
 
#define MAX(x, y) ((x) > (y) ? (x) : (y))  
/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)                  

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 
 
/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

/* Given block ptr bp, compute address of next and previous free blocks in the free list */
#define PREV_FBKP(bp) (void *)(*(size_t *) (bp))
#define NEXT_FBKP(bp) (void *)(*(size_t *) (bp + WSIZE))

/* Write the address of next and previous free blocks in the free list */
#define PUT_PREV_FREE(bp, previous) (*((unsigned int *)(bp)) = previous)
#define PUT_NEXT_FREE(bp, next) (*((unsigned int *)(bp + WSIZE)) = next)

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void insert_block(void *bp);
static void remove_block(void *bp);
int mm_check(void);
/* Pointers required for maintaining free list */
void *heap_listp;
void *root;//Root of the free list

/* 
 * mm_init - initialize the malloc package.
 */ 
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) 
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp+WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp+DSIZE, PACK(DSIZE, 1));
    PUT(heap_listp+WSIZE+DSIZE, PACK(0, 1));
    heap_listp += DSIZE;
    root=NULL; // Initialize root to NULL 
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == (void *)-1)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Allocate a block whose size is a multiple of the alignment.
 *     Adjust block size if it is not a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;
    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE) 
        asize = 2*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 
    /* Search the free list for a fit */
    if ((bp = find_fit(asize))) {  
        place(bp, asize);                  
        return bp;
    }
    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    place(bp, asize);                                 
    return bp;
}

/*
 * mm_free - Freeing a block.
        Coalesce the freed block with contigous free blocks.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - If ptr is NULL and size is not zero, the call is equivalent to mm_malloc(size).
        If size is equal to zero, the call is equivalent to mm_free(ptr).
        Else, copy the old data to the new block, change the size of the block and return the address of new block.
 */
void *mm_realloc(void *ptr, size_t size)
{
    /* If ptr is NULL and size is not zero, the call is equivalent to mm_malloc(size). */
    if(ptr == NULL && size != 0) {
        return mm_malloc(size);
    }
    /* If size is equal to zero, the call is equivalent to mm_free(ptr). */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }
    size_t oldsize;
    void *newptr;
    if(!(newptr = mm_malloc(size))) return 0;
    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);
    /* Free the old block. */
    mm_free(ptr);
    return newptr;
}

/*
 * coalesce - Coalesce contigous free blocks.
 */
static void *coalesce(void *bp) 
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* Case 1 */
        insert_block(bp);
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        remove_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {                                     /* Case 4 */
        remove_block(NEXT_BLKP(bp));
        remove_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert_block(bp);
    return bp;
}

/*
 * extend_heap - Extend the heap with a free block of size bytes.
 */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 
    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

/*
 * find_fit - Find a fit free block in the free block list with the least size of no less than asize.
 */
static void *find_fit(size_t asize){
    void *bp = root;
    /* From root, find the first fit with a size of asize */
    while(bp){
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) return bp;
        bp = (void *)NEXT_FBKP(bp);
    }
    return NULL;
}

/*
 * place - Alloctae a free block in the free block list, split it if the remaining block has a size over 16 bytes.
 */
static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));   
    /* If remaining size if larger than 16 bytes, split the current free block */
    if ((csize - asize) >= (2*DSIZE)) {
        remove_block(bp); 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        insert_block(bp);
    }
    else { 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        remove_block(bp);
    }
}

/* 
 * insert_block - Insert an empty block into the free block list.
 */
void insert_block(void *bp){ 
    void *self = root;
    void *next = self;
    void *prev = NULL;
    while (self != NULL && bp < self) { 
        prev = PREV_FBKP(self);
        next = self;
        self = NEXT_FBKP(self);
    }
    PUT_PREV_FREE(bp, (int)prev);
    PUT_NEXT_FREE(bp, (int)next);
    if (prev != NULL) {
        PUT_NEXT_FREE(prev, (int)bp);
    } else { 
        root = bp;
    }
    if (next != NULL) {
        PUT_PREV_FREE(next, (int)bp);
    }
}

/* 
 * remove_block - Remove an empty block from the free block list.
 */
void remove_block(void *bp){ 
    void *next = NEXT_FBKP(bp);
    void *prev = PREV_FBKP(bp);
    if (prev == NULL) { 
        root = next;
    } else {
        PUT_NEXT_FREE(prev, (int)next);
    }

    if (next != NULL) { 
        PUT_PREV_FREE(next, (int)prev);
    }

}

/*
 * mm_check - Scan the heap and checks it for consistency.
 */
int mm_check(void){ 
    /* Is every block in the free list marked as free? */
    void *bp = root, *fp = root;
    while(bp){
        if(GET_ALLOC(HDRP(bp))) {
            printf("A block in the free list marked as allocated!\n");
            return 0;
        }
        bp = NEXT_FBKP(bp);
    }
    /* Are there any contigous free blocks that somehow escaped coalescing? */
    bp = heap_listp;
    while(bp && GET_SIZE(HDRP(bp)) > 0) {
        if(!GET_ALLOC(HDRP(bp))){
            if(!GET_ALLOC(FTRP(PREV_BLKP(bp))) || !GET_ALLOC(HDRP(NEXT_BLKP(bp)))){
                printf("There are free blocks that somehow escaped coalescing!\n");
                return 0;
            }
        }
        bp = NEXT_BLKP(bp);
    }
    /* Is every free block actually in the free list? */
    bp = heap_listp;
    while(bp && GET_SIZE(HDRP(bp)) > 0){
        if(!GET_ALLOC(HDRP(bp))){
            fp = root;
            while(fp){
                if(fp == bp) break;
                fp = NEXT_FBKP(fp);
            }
            if(!fp) {
                printf("A free block is not in the free list!\n");
                return 0;
            }
        }
        bp = NEXT_BLKP(bp);
    }
    /* Do the pointers in the free list point to valid free blocks? */
    bp = root;
    while(bp){
        if((GET_SIZE(HDRP(bp)) % DSIZE) != 0 ){
            printf("A pointer in the free list points to invalid free block!\n");
            return 0;
        }
        bp = NEXT_FBKP(bp);
    }
    return 1;
}




