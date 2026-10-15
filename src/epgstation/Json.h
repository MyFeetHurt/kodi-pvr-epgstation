/*
 *  Copyright (C) 2026 MyFeetHurt
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

// Lenient field access for EPGStation's JSON: a missing, null or mistyped optional field gives
// the fallback instead of throwing (nlohmann's value() throws on a present field of another type).

#include <string>

#include <nlohmann/json.hpp>

namespace epgstation::json
{

inline std::string StringOr(const nlohmann::json& j, const char* key, std::string fallback = {})
{
  return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}

template<typename T>
T NumberOr(const nlohmann::json& j, const char* key, T fallback)
{
  return j.contains(key) && j[key].is_number_integer() ? j[key].get<T>() : fallback;
}

inline bool BoolOr(const nlohmann::json& j, const char* key, bool fallback)
{
  return j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : fallback;
}

// true only for a present `true`.
inline bool Flag(const nlohmann::json& j, const char* key)
{
  return BoolOr(j, key, false);
}

} // namespace epgstation::json
