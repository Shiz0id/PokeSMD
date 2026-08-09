#ifndef GUARD_ALLOC_H
#define GUARD_ALLOC_H


#define FREE_AND_SET_NULL(ptr)          \
do {                                    \
    Free(ptr);                          \
    ptr = NULL;                         \
} while (0)

#define TRY_FREE_AND_SET_NULL(ptr) if (ptr != NULL) FREE_AND_SET_NULL(ptr)

#define MALLOC_SYSTEM_ID 0xA3A3

struct MemBlock
{
    // Whether this block is currently allocated.
    u16 allocated:1;

    u16 unused_00:4;

    // High 11 bits of location pointer.
    u16 locationHi:11;

    // Magic number used for error checking. Should equal MALLOC_SYSTEM_ID.
    u16 magic;

    // Size of the block (not including this header struct).
    u32 size:18;

    // Low 14 bits of location pointer.
    u32 locationLo:14;

    // Previous block pointer. Equals sHeapStart if this is the first block.
    struct MemBlock *prev;

    // Next block pointer. Equals sHeapStart if this is the last block.
    struct MemBlock *next;

    // Data in the memory block. (Arrays of length 0 are a GNU extension.)
    u8 data[0];
};

// 0x1C500 + 0x2000. The BW animated sprites reserve 4 KB per animating
// battler and a double battle has four, so 16 KB of what used to be
// headroom is spoken for before anything else asks. That budget was
// measured once, when the frame containers landed, and then the BW battle
// UI, the SwSh bag, the USM start menu, the EV allocator, the registered
// items menu and the fishing minigame all arrived on the same heap -- none
// of which the linker can see, because the heap is one fixed array.
//
// Two 4096-byte requests were failing in doubles: the move box's tilemap
// on the FIGHT button (src/menu.c:1592) and gBattleAnimBgTilemapBuffer on
// the way back from the bag (src/battle_util2.c:20).
#define HEAP_SIZE 0x1E500
extern u8 gHeap[HEAP_SIZE];

#if TESTING || !defined(NDEBUG)

#define Alloc(size) Alloc_(size, __FILE__ ":" STR(__LINE__))
#define AllocUnchecked(size) AllocUnchecked_(size, __FILE__ ":" STR(__LINE__))

#define AllocZeroed(size) AllocZeroed_(size, __FILE__ ":" STR(__LINE__))
#define AllocZeroedUnchecked(size) AllocZeroedUnchecked_(size, __FILE__ ":" STR(__LINE__))

#else

#define Alloc(size) Alloc_(size, NULL)
#define AllocUnchecked(size) AllocUnchecked_(size, NULL)
#define AllocZeroed(size) AllocZeroed_(size, NULL)
#define AllocZeroedUnchecked(size) AllocZeroedUnchecked_(size, NULL)

#endif

void *Alloc_(u32 size, const char *location);
void *AllocUnchecked_(u32 size, const char *location);
void *AllocZeroed_(u32 size, const char *location);
void *AllocZeroedUnchecked_(u32 size, const char *location);
void Free(void *pointer);
void InitHeap(void *heapStart, u32 heapSize);
void PrintHeap(void);

const struct MemBlock *HeapHead(void);
const char *MemBlockLocation(const struct MemBlock *block);

#endif // GUARD_ALLOC_H
