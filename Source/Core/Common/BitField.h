// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// Copyright 2014 Tony Wasserka
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the owner nor the names of its contributors may
//       be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#pragma once

#include <limits>
#include <tuple>
#include <type_traits>

#include "Common.h"

/*
 * Abstract bitfield class
 *
 * Allows endianness-independent access to individual bitfields within some raw
 * integer value. The assembly generated by this class is identical to the
 * usage of raw bitfields, so it's a perfectly fine replacement.
 *
 * For BitField<X,Y,Z>, X is the distance of the bitfield to the LSB of the
 * raw value, Y is the length in bits of the bitfield. Z is an integer type
 * which determines the sign of the bitfield. Z must have the same size as the
 * raw integer.
 *
 *
 * General usage:
 *
 * Create a new union with the raw integer value as a member.
 * Then for each bitfield you want to expose, add a BitField member
 * in the union. The template parameters are the bit offset and the number
 * of desired bits.
 *
 * Changes in the bitfield members will then get reflected in the raw integer
 * value and vice-versa.
 *
 *
 * Sample usage:
 *
 * union SomeRegister
 * {
 *     u32 hex;
 *
 *     BitField<0,7,u32> first_seven_bits;     // unsigned
 *     BitField<7,8,32> next_eight_bits;       // unsigned
 *     BitField<3,15,s32> some_signed_fields;  // signed
 * };
 *
 * This is equivalent to the little-endian specific code:
 *
 * union SomeRegister
 * {
 *     u32 hex;
 *
 *     struct
 *     {
 *         u32 first_seven_bits : 7;
 *         u32 next_eight_bits : 8;
 *     };
 *     struct
 *     {
 *         u32 : 3; // padding
 *         s32 some_signed_fields : 15;
 *     };
 * };
 *
 *
 * Caveats:
 *
 * 1)
 * BitField provides automatic casting from and to the storage type where
 * appropriate. However, when using non-typesafe functions like printf, an
 * explicit cast must be performed on the BitField object to make sure it gets
 * passed correctly, e.g.:
 * printf("Value: %d", (s32)some_register.some_signed_fields);
 *
 * 2)
 * Not really a caveat, but potentially irritating: This class is used in some
 * packed structures that do not guarantee proper alignment. Therefore we have
 * to use #pragma pack here not to pack the members of the class, but instead
 * to break GCC's assumption that the members of the class are aligned on
 * sizeof(StorageType).
 * TODO(neobrain): Confirm that this is a proper fix and not just masking
 * symptoms.
 */
#pragma pack(1)
template<std::size_t position, std::size_t bits, typename T>
struct BitField
{
private:
	// This constructor might be considered ambiguous:
	// Would it initialize the storage or just the bitfield?
	// Hence, delete it. Use the assignment operator to set bitfield values!
	BitField(T val) = delete;

public:
	// Force default constructor to be created
	// so that we can use this within unions
	BitField() = default;

	__forceinline BitField& operator=(T val)
	{
		storage = (storage & ~GetMask()) | ((val << position) & GetMask());
		return *this;
	}

	__forceinline operator T() const
	{
		if (std::numeric_limits<T>::is_signed)
		{
			std::size_t shift = 8 * sizeof(T) - bits;
			return (T)(((storage & GetMask()) << (shift - position)) >> shift);
		}
		else
		{
			return (T)((storage & GetMask()) >> position);
		}
	}

	typedef T UnderlyingType;

private:
	// StorageType is T for non-enum types and the underlying type of T if
	// T is an enumeration. Note that T is wrapped within an enable_if in the
	// former case to workaround compile errors which arise when using
	// std::underlying_type<T>::type directly.
	typedef typename std::conditional<std::is_enum<T>::value,
	                                  std::underlying_type<T>,
	                                  std::enable_if<true,T>>::type::type StorageType;

	// Unsigned version of StorageType
	typedef typename std::make_unsigned<StorageType>::type StorageTypeU;

	__forceinline StorageType GetMask() const
	{
		return ((~(StorageTypeU)0) >> (8*sizeof(T) - bits)) << position;
	}

	StorageType storage;

	static_assert(bits + position <= 8 * sizeof(T), "Bitfield out of range");

	// And, you know, just in case people specify something stupid like bits=position=0x80000000
	static_assert(position < 8 * sizeof(T), "Invalid position");
	static_assert(bits <= 8 * sizeof(T), "Invalid number of bits");
	static_assert(bits > 0, "Invalid number of bits");
};
#pragma pack()

/*
 * Allows grouping multiple BitField objects into an array
 *
 * Get() behaves exactly like the [] operator of a regular array, but
 * it can only be used if the array index is known at compile time (as a
 * constexpr). If that is not the case, GetValue and SetValue allow modifying
 * the bitfields. Do note that the latter functions might incur a minimal
 * performance overhead, especially if the compiler cannot determine the
 * index value on compile time.
 *
 * Usage:
 *
 * Given a union (or structure) called SomeStructure containing any BitFields,
 * the bitfields can be grouped by adding a new method returning a
 * BitFieldArray object, e.g.:
 *
 * struct SomeStructure
 * {
 *     u32 hex;
 *     BitField<0, 8, u32> field1;
 *     BitField<8, 2, u32> field2;
 *     BitField<10, 5, u32> field3;
 *
 *     DECLARE_BITFIELD_ARRAY(GetFieldArray, field1, field2, field3);
 * };
 *
 */
template<typename... BitFields>
class BitFieldArray
{
private:
	// TODO: Assert that all tuple bitfields use the same underlying type
	std::tuple<BitFields&...> bitfields;

	// Helper functions to create sub-tuples
	template<unsigned...s> struct seq { typedef seq<s...>& type; };
	template<unsigned max, unsigned... s> struct make_seq:make_seq<max-1, max-1, s...> {};
	template<unsigned...s> struct make_seq<0, s...>:seq<s...> {};

	template<unsigned... s, typename Tuple>
	static auto extract_tuple(seq<s...>, Tuple& tup) -> decltype(std::tie(std::get<s>(tup)...))
	{
		return std::tie(std::get<s>(tup)...);
	}

	// ArrayElement: Private variadic template class used to deal with indexable tuples with the common base type UnderlyingType.
	// Implemented via recursion, with the common members stored in ArrayElementBase.
	template<typename... BitFields_2>
	class ArrayElementBase
	{
	protected:
		ArrayElementBase(size_t idx, std::tuple<BitFields_2&...> bfs) : index(idx), fields(bfs) {}

		const size_t index;
		const std::tuple<BitFields_2&...> fields;
		typedef typename std::tuple_element<0,std::tuple<BitFields_2...>>::type::UnderlyingType UnderlyingType;
	};

	template<typename B1_2, typename... BitFields_2>
	class ArrayElement : public ArrayElementBase<B1_2, BitFields_2...>
	{
		typedef typename ArrayElementBase<B1_2, BitFields_2...>::UnderlyingType UnderlyingType;

//		static_assert(typeid(typename std::tuple_element<0,std::tuple<BitFields_2...>>::type::UnderlyingType) ==
//		              typeid(typename std::tuple_element<1,std::tuple<BitFields_2...>>::type::UnderlyingType), "Typeids mismatch!");

	public:
		ArrayElement(size_t idx, std::tuple<B1_2&, BitFields_2&...> bfs) : ArrayElementBase<B1_2, BitFields_2...>(idx, bfs)
		{
		}

		// TODO: Not sure if it's a good idea to expose this operator.
		__forceinline ArrayElement& operator=(const ArrayElement& other)
		{
			*this = (UnderlyingType)other;
			return *this;
		}

		__forceinline ArrayElement& operator=(const UnderlyingType& val)
		{
			// TODO: I don't get it. shouldn't this be == 0?
			if (this->index == sizeof...(BitFields_2))
			{
				std::get<sizeof...(BitFields_2)>(this->fields) = val;
			}
			else
			{
				MakeArrayElementFromTuple(this->index, extract_tuple(make_seq<sizeof...(BitFields_2)>(), this->fields)) = val;
			}
			return *this;
		}

		__forceinline operator UnderlyingType() const
		{
			if (this->index == sizeof...(BitFields_2))
			{
				return std::get<sizeof...(BitFields_2)>(this->fields);
			}
			else
			{
				auto sub_tuple = extract_tuple(make_seq<sizeof...(BitFields_2)>(), this->fields);
				return MakeArrayElementFromTuple(this->index, sub_tuple);
			}
		}
	};

	template<typename B1_2>
	class ArrayElement<B1_2> : public ArrayElementBase<B1_2>
	{
		typedef typename ArrayElementBase<B1_2>::UnderlyingType UnderlyingType;

	public:
		ArrayElement(size_t idx, std::tuple<B1_2&> bfs) : ArrayElementBase<B1_2>(idx, bfs)
		{
		}

		// TODO: Not sure if it's a good idea to expose this operator.
		__forceinline ArrayElement& operator=(const ArrayElement& other)
		{
			*this = (UnderlyingType)other;
			return *this;
		}

		__forceinline ArrayElement& operator=(UnderlyingType val)
		{
			std::get<0>(this->fields) = val;
			return *this;
		}

		__forceinline operator UnderlyingType() const
		{
			return std::get<0>(this->fields);
		}
	};

	template<typename... BitFields_2>
	static ArrayElement<BitFields_2...> MakeArrayElementFromTuple(size_t index, std::tuple<BitFields_2&...> bfs)
	{
		return ArrayElement<BitFields_2...>(index, bfs);
	}

public:
	BitFieldArray(std::tuple<BitFields&...> tup) : bitfields(tup)
	{
	}

	// TODO: This doesn't actually return a reference
	template<size_t index>
	auto Get() const -> typename std::add_const<typename std::tuple_element<index,decltype(bitfields)>::type>::type
	{
		return std::get<index>(bitfields);
	}

	ArrayElement<BitFields...> operator[](size_t index) const
	{
		return MakeArrayElementFromTuple(index, bitfields);
	}
};


template<typename... BitFields>
static inline BitFieldArray<BitFields...> MakeBitFieldArrayFromTupleConst(const std::tuple<BitFields&...> bfs)
{
	return BitFieldArray<BitFields...>(bfs);
}

template<typename... BitFields>
static inline BitFieldArray<BitFields...> MakeBitFieldArrayFromTuple(std::tuple<BitFields&...> bfs)
{
	return BitFieldArray<BitFields...>(bfs);
}


#define DECLARE_BITFIELD_ARRAY(name, ...) \
	inline auto name() const -> decltype(MakeBitFieldArrayFromTupleConst(std::tie(__VA_ARGS__))) \
	{ \
		return MakeBitFieldArrayFromTupleConst(std::tie(__VA_ARGS__)); \
	} \
	inline auto name() -> decltype(MakeBitFieldArrayFromTuple(std::tie(__VA_ARGS__))) \
	{ \
		return MakeBitFieldArrayFromTuple(std::tie(__VA_ARGS__)); \
	}
