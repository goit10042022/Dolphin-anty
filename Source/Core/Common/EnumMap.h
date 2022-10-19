// Copyright 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <type_traits>

#include "Common/BitFieldView.h"
#include "Common/Concepts.h"

template <std::size_t position, std::size_t bits, typename T, typename StorageType>
struct BitField;

namespace Common
{
// A type that allows lookup of values associated with an enum as the key.
// Designed for enums whose numeric values start at 0 and increment continuously with few gaps.
template <typename V, auto last_member, typename = decltype(last_member)>
class EnumMap final
{
  // The third template argument is needed to avoid compile errors from ambiguity with multiple
  // enums with the same number of members in GCC prior to 8.  See https://godbolt.org/z/xcKaW1seW
  // and https://godbolt.org/z/hz7Yqq1P5
  using T = decltype(last_member);
  static_assert(std::is_enum_v<T>);
  static constexpr size_t s_size = static_cast<size_t>(last_member) + 1;

  using array_type = std::array<V, s_size>;
  using iterator = typename array_type::iterator;
  using const_iterator = typename array_type::const_iterator;

public:
  constexpr EnumMap() = default;
  constexpr EnumMap(const EnumMap& other) = default;
  constexpr EnumMap& operator=(const EnumMap& other) = default;
  constexpr EnumMap(EnumMap&& other) = default;
  constexpr EnumMap& operator=(EnumMap&& other) = default;

  template <typename... Ts>
  requires CountOfTypes<s_size, Ts...> && ConvertibleFromAllOf<V, Ts...>
  constexpr EnumMap(Ts... values) : m_array{static_cast<V>(values)...} {}

  constexpr const V& operator[](T key) const { return m_array[static_cast<std::size_t>(key)]; }
  constexpr V& operator[](T key) { return m_array[static_cast<std::size_t>(key)]; }

  // These only exist to perform the safety check; without them, BitField's implicit conversion
  // would work (but since BitField is used for game-generated data, we need to be careful about
  // bounds-checking)
  template <std::size_t position, std::size_t bits, typename StorageType>
  constexpr const V& operator[](BitField<position, bits, T, StorageType> key) const
  {
    static_assert(size_t{1} << bits == s_size,
                  "Unsafe indexing into EnumMap (may go out of bounds)");
    return m_array[static_cast<std::size_t>(key.Value())];
  }
  template <std::size_t position, std::size_t bits, typename StorageType>
  constexpr V& operator[](BitField<position, bits, T, StorageType> key)
  {
    static_assert(size_t{1} << bits == s_size,
                  "Unsafe indexing into EnumMap (may go out of bounds)");
    return m_array[static_cast<std::size_t>(key.Value())];
  }

  // This only exists to perform the safety check; without them, A BitFieldView's implicit
  // conversion would work (but since BitFieldViews are used for game-generated data, we need
  // to be careful about bounds-checking)
  template <IntegralOrEnum field_t, std::size_t width, std::size_t start, typename host_t>
  constexpr const V& operator[](const MutFixedBitFieldView<field_t, width, start, host_t> key) const
  {
    static_assert(size_t{1} << width == s_size,
                  "Unsafe indexing into EnumMap (may go out of bounds)");
    return m_array[static_cast<std::size_t>(key.Get())];
  }
  template <IntegralOrEnum field_t, std::size_t width, std::size_t start, typename host_t>
  constexpr V& operator[](const MutFixedBitFieldView<field_t, width, start, host_t> key)
  {
    static_assert(size_t{1} << T::BitWidth() == s_size,
                  "Unsafe indexing into EnumMap (may go out of bounds)");
    return m_array[static_cast<std::size_t>(key.Get())];
  }

  constexpr bool InBounds(T key) const { return static_cast<std::size_t>(key) < s_size; }

  constexpr size_t size() const noexcept { return s_size; }

  constexpr V* data() { return m_array.data(); }
  constexpr const V* data() const { return m_array.data(); }

  constexpr iterator begin() { return m_array.begin(); }
  constexpr iterator end() { return m_array.end(); }
  constexpr const_iterator begin() const { return m_array.begin(); }
  constexpr const_iterator end() const { return m_array.end(); }
  constexpr const_iterator cbegin() const { return m_array.cbegin(); }
  constexpr const_iterator cend() const { return m_array.cend(); }

  constexpr void fill(const V& v) { m_array.fill(v); }

private:
  array_type m_array{};
};
}  // namespace Common
