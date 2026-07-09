// Copyright 2026 Paiza. All rights reserved.

#include "core/transliterator.h"

#include "core/key_mapping.h"

namespace paiza {
namespace {

const KeyMapping& DefaultMapping() {
  static const KeyMapping mapping = KeyMapping::CreateDefault();
  return mapping;
}

}  // namespace

std::wstring Transliterator::Convert(std::string_view latin) {
  return DefaultMapping().Convert(latin);
}

bool Transliterator::IsComposableChar(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

}  // namespace paiza
