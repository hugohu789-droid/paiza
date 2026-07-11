// Copyright 2026 Paiza. All rights reserved.
//
// On-disk layout of the compiled static lexicon (lexicon.bin), shared by the
// offline compiler (writer) and StaticLexicon (mmap reader). All integers are
// little-endian; the header carries an endianness mark so a wrong-endian file
// is rejected instead of misread. The CRC32 covers everything after the
// header, so a truncated or corrupted file is rejected wholesale — the engine
// then falls back to the plain-text dictionary rather than half-loading.
//
// Layout:
//   LexiconHeader
//   KeyRecord[key_count]        sorted by key bytes (implicit trie)
//   key blob                    concatenated Latin keys (no terminators)
//   EntryRecord[entry_count]    grouped per key, frequency-descending
//   string pool                 concatenated UTF-8 Mongolian words
//   uint32[bigram_row_count+1]  CSR row offsets into the edge array
//   BigramEdge[bigram_edge_count]  per row sorted by weight descending
//
// word_id of an entry = its EntryRecord index + 1 (0 means "no word").

#ifndef PAIZA_CORE_LEXICON_BINARY_H_
#define PAIZA_CORE_LEXICON_BINARY_H_

#include <cstddef>
#include <cstdint>

namespace paiza {

inline constexpr uint32_t kLexiconMagic = 0x584C5A50;  // "PZLX"
inline constexpr uint32_t kLexiconVersion = 1;
inline constexpr uint32_t kLexiconEndianMark = 0x01020304;

#pragma pack(push, 1)

struct LexiconHeader {
  uint32_t magic = kLexiconMagic;
  uint32_t version = kLexiconVersion;
  uint32_t endian_mark = kLexiconEndianMark;
  uint32_t payload_crc32 = 0;  // CRC of every byte after this header.
  uint32_t key_count = 0;
  uint32_t entry_count = 0;
  uint32_t bigram_row_count = 0;   // entry_count when bigrams present, else 0.
  uint32_t bigram_edge_count = 0;
  uint64_t keys_offset = 0;        // All offsets are from file start.
  uint64_t key_blob_offset = 0;
  uint64_t key_blob_size = 0;
  uint64_t entries_offset = 0;
  uint64_t string_pool_offset = 0;
  uint64_t string_pool_size = 0;
  uint64_t bigram_index_offset = 0;
  uint64_t bigram_edges_offset = 0;
  uint64_t file_size = 0;
  uint64_t reserved = 0;
};

struct KeyRecord {
  uint32_t key_offset = 0;  // Into the key blob.
  uint32_t key_length = 0;
  uint32_t entry_first = 0;  // Index of the first EntryRecord for this key.
  uint32_t entry_count = 0;
};

struct EntryRecord {
  uint32_t text_offset = 0;  // Into the string pool (UTF-8).
  uint32_t text_length = 0;
  uint32_t frequency = 0;
  uint32_t key_index = 0;  // Owning KeyRecord (entry -> Latin key lookup).
};

struct BigramEdge {
  uint32_t word_id2 = 0;  // Follow-up word (EntryRecord index + 1).
  uint32_t weight = 0;    // Co-occurrence count / strength.
};

#pragma pack(pop)

static_assert(sizeof(LexiconHeader) == 112, "header layout is the contract");
static_assert(sizeof(KeyRecord) == 16, "key record layout is the contract");
static_assert(sizeof(EntryRecord) == 16, "entry layout is the contract");
static_assert(sizeof(BigramEdge) == 8, "edge layout is the contract");

// CRC-32 (IEEE 802.3, the zlib polynomial), used for payload validation.
uint32_t Crc32(const uint8_t* data, size_t size);

}  // namespace paiza

#endif  // PAIZA_CORE_LEXICON_BINARY_H_
