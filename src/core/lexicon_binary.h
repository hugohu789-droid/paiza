// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 hugohu789-droid
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
// v2 adds the DAWG section; v3 the equivalence-rules blob; v4 the GB/T
// fixed-sequence rules blob.
inline constexpr uint32_t kLexiconVersion = 4;
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
  // DAWG section (v2). dawg_node_count == 0 means "no DAWG" (older/degenerate
  // file); the reader then falls back to a flat key-table tolerant search.
  uint64_t dawg_nodes_offset = 0;
  uint64_t dawg_trans_offset = 0;
  // Equivalence-rules blob (v3): the raw equivalences.txt text, parsed by the
  // reader into soft-equivalence classes. equiv_blob_size == 0 means none.
  uint64_t equiv_blob_offset = 0;
  // GB/T fixed-sequence rules blob (v4): raw fixed_sequences.txt text for the
  // compose layer. fixed_blob_size == 0 means none.
  uint64_t fixed_blob_offset = 0;
  uint32_t dawg_node_count = 0;
  uint32_t dawg_trans_count = 0;
  uint32_t dawg_root = 0;
  uint32_t equiv_blob_size = 0;
  uint32_t fixed_blob_size = 0;
  uint32_t reserved = 0;
};

// A DAWG state. Transitions live contiguously in the trans array, sorted by
// label ascending. word_count is the number of keys accepted from this state,
// used to derive the lexicographic rank (= KeyRecord index) of a match.
struct DawgNode {
  uint32_t trans_first = 0;
  uint32_t trans_count = 0;
  uint32_t word_count = 0;
  uint32_t is_final = 0;  // 0/1 (uint32 keeps the struct naturally aligned).
};

struct DawgTrans {
  uint8_t label = 0;
  uint8_t pad[3] = {0, 0, 0};
  uint32_t target = 0;  // Destination node index.
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

static_assert(sizeof(LexiconHeader) == 160, "header layout is the contract");
static_assert(sizeof(KeyRecord) == 16, "key record layout is the contract");
static_assert(sizeof(EntryRecord) == 16, "entry layout is the contract");
static_assert(sizeof(BigramEdge) == 8, "edge layout is the contract");
static_assert(sizeof(DawgNode) == 16, "dawg node layout is the contract");
static_assert(sizeof(DawgTrans) == 8, "dawg trans layout is the contract");

// CRC-32 (IEEE 802.3, the zlib polynomial), used for payload validation.
uint32_t Crc32(const uint8_t* data, size_t size);

}  // namespace paiza

#endif  // PAIZA_CORE_LEXICON_BINARY_H_
