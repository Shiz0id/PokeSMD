#ifndef GUARD_DECOMPRESS_H
#define GUARD_DECOMPRESS_H

#include "sprite.h"

#define MAX_DECOMPRESSION_BUFFER_SIZE 0x4000

#define TANS_TABLE_SIZE     64
#define PACKED_FREQ_MASK    0x3F
#define PARTIAL_FREQ_MASK   0xC0000000

#define FIRST_LO_MASK   0x7f
#define CONTINUE_BIT    0x80

#define SMOL_IMAGE_SIZE_MULTIPLIER 4

struct LZ77Header {
    u32 lz77IdBits:5;
    u32 padding:3;
    u32 size:24;
};

struct SmolHeader {
    u32 mode:4;
    u32 imageSize:14;
    u32 symSize:14;
    u32 initialState:6;
    u32 bitstreamSize:13;
    u32 loSize:13;
};

//  Frame container, mode 7. A stack of equally sized frames in which every
//  group of framesPerComponent consecutive frames is a complete standalone smol
//  blob, so reaching frame N decodes only the chunk holding it.
//
//  Header, then one u32 word-offset per chunk, then the chunks:
//
//      word 0                  this struct, first word
//      word 1                  this struct, second word
//      word 2 .. 2+numComponents-1     chunk offsets, in WORDS from word 0
//      ...                     the chunks
//
//  numComponents is the chunk count and framesPerComponent the frames in each;
//  only the last chunk may hold fewer, which is why totalFrames is carried
//  rather than derived.
struct SpriteSheetHeader {
    u32 mode:4;
    u32 numComponents:12;
    u32 framesPerComponent:16;
    u32 frameSize:16;
    u32 totalFrames:16;
};

#define FRAME_CONTAINER_HEADER_WORDS 2

struct SmolTilemapHeader {
    u32 mode:4;
    u32 tilemapSize:14;
    u32 symSize:14;
    u32 tileNumberSize;
};

union CompressionHeader {
    struct LZ77Header lz77;
    struct SmolHeader smol;
    struct SmolTilemapHeader smolTilemap;
    struct SpriteSheetHeader frameContainer;
};

enum CompressionMode {
    MODE_LZ77 = 0,
    BASE_ONLY = 1,
    ENCODE_SYMS = 2,
    ENCODE_DELTA_SYMS = 3,
    ENCODE_LO = 4,
    ENCODE_BOTH = 5,
    ENCODE_BOTH_DELTA_SYMS = 6,
    IS_FRAME_CONTAINER = 7,
    IS_TILEMAP = 8,
};

void DecompressDataWithHeaderVram(const u32 *src, void *dest);
void DecompressDataWithHeaderWram(const u32 *src, void *dest);

//  Frame containers. These are the whole API for mode 7 - the two wrappers
//  above deliberately reject it, because a container has no single destination
//  size and decoding one is a per-chunk operation.
//
//  A caller that keeps the last decoded chunk pays one decode per chunk rather
//  than one per frame, which is the reason the format exists. The usual shape:
//
//      chunk = GetSmolFrameChunk(src, frame);
//      if (chunk != cachedChunk)
//      {
//          DecompressSmolChunk(src, buffer, chunk);   // buffer is
//          cachedChunk = chunk;                       // GetSmolChunkSize(src)
//      }
//      frameData = buffer + GetSmolFrameOffsetInChunk(src, frame);
bool32 IsSmolFrameContainer(const u32 *src);
u32 GetSmolFrameCount(const u32 *src);
u32 GetSmolFrameSize(const u32 *src);
u32 GetSmolChunkSize(const u32 *src);
u32 GetSmolFrameChunk(const u32 *src, u32 frame);
u32 GetSmolFrameOffsetInChunk(const u32 *src, u32 frame);
void DecompressSmolChunk(const u32 *src, void *dest, u32 chunk);

// Lucky's fast lz decompression function
void FastLZ77UnCompWram(const u32 *src, void *dest);

//  Default Decompression functions are below here
u32 IsLZ77Data(const void *ptr, u32 minSize, u32 maxSize);

u32 LoadCompressedSpriteSheet(const struct CompressedSpriteSheet *src);
u32 LoadCompressedSpriteSheetByTemplate(const struct SpriteTemplate *template, s32 offset);
u32 LoadCompressedSpriteSheetOverrideBuffer(const struct CompressedSpriteSheet *src, void *buffer);
bool8 LoadCompressedSpriteSheetUsingHeap(const struct CompressedSpriteSheet *src);

void HandleLoadSpecialPokePic(bool32 isFrontPic, void *dest, enum Species species, u32 personality);
void HandleLoadSpecialPokePicIsEgg(bool32 isFrontPic, void *dest, enum Species species, u32 personality, bool32 isEgg);

void LoadSpecialPokePic(void *dest, enum Species species, u32 personality, bool8 isFrontPic);
void LoadSpecialPokePicIsEgg(void *dest, enum Species species, u32 personality, bool8 isFrontPic, bool32 isEgg);

u32 GetDecompressedDataSize(const u32 *ptr);
bool32 IsCompressedData(const u32 *ptr);

#endif // GUARD_DECOMPRESS_H
