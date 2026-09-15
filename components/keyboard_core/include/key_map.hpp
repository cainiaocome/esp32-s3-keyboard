#pragma once

#include <cstddef>

#include "key_codes.hpp"

namespace remote_hid {

// Names are case-insensitive and use the canonical names documented by the
// API. The parser accepts a small set of common modifier aliases as well.
bool key_from_name(const char* name, KeyCode& key);
const char* key_name(KeyCode key);
bool is_valid_key(KeyCode key);

}  // namespace remote_hid
