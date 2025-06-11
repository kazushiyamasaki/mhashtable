/*
 * mhashtable.h -- interface of a simple and thread-safe hashtable library
 * version 0.9.0, June 12, 2025
 *
 * License: zlib License
 *
 * Copyright (c) 2025 Kazushi Yamasaki
 *
 * This software is provided ‘as-is’, without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 * distribution.
 *
 *
 *
 * IMPORTANT:
 * If a pointer to another hashtable is stored directly as a value in a hashtable,
 * do not use ht_destroy to destroy the outer hashtable. Instead, use
 * ht_destroy_without_value.
 * Conversely, do not use ht_destroy_without_value to destroy the inner (pointed-to)
 * hashtable use ht_destroy instead.
 * However, if you store a pointer to a hashtable inside a struct, and that struct
 * is stored as a value in another hashtable, you must use ht_destroy to properly
 * destroy both hashtables.
 *
 * The functions ht_create, ht_destroy, ht_destroy_without_value, ht_set, ht_set_raw,
 * ht_get, ht_all_get, and ht_delete are also implemented as macros and cannot be
 * passed to function pointers.
 * If you need to use these functions with function pointers, please use the actual
 * functions they expand to, which are prefixed with _.
 *
 *
 * Note:
 * This library uses a global lock to ensure thread safety. As a result, performance
 * may degrade significantly when accessed concurrently by many threads.
 * In high-load environments or those with many threads, it is recommended to design
 * your application to minimize simultaneous access whenever possible.
 *
 * To enable debug mode, define DEBUG macro before including this file.
 */

#pragma once

#ifndef MHASHTABLE_H
#define MHASHTABLE_H


#ifndef MHT_CPP_C_BEGIN
	#ifdef __cplusplus  /* C++ */
		#define MHT_CPP_C_BEGIN extern "C" {
		#define MHT_CPP_C_END }
	#else               /* not C++ */
		#define MHT_CPP_C_BEGIN
		#define MHT_CPP_C_END
	#endif
#endif


MHT_CPP_C_BEGIN


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


/*
 * The functions replaced by the following macros are specific to this library.
 * It is recommended not to remove them unless a conflict occurs.
 */
#define ht_create(size) _ht_create((size), __FILE__, __LINE__)
#define ht_destroy(ht) _ht_destroy((ht), __FILE__, __LINE__)
#define ht_destroy_without_value(ht) _ht_destroy_without_value((ht), __FILE__, __LINE__)
#define ht_set(ht, key, value_data, value_size) _ht_set((ht), (key), (value_data), (value_size), __FILE__, __LINE__)
#define ht_set_raw(ht, key, value_data) _ht_set_raw((ht), (key), (value_data), __FILE__, __LINE__)
#define ht_get(ht, key) _ht_get((ht), (key), __FILE__, __LINE__)
#define ht_all_get(ht, out_count) _ht_all_get((ht), (out_count), __FILE__, __LINE__)
#define ht_all_release_arr(values) _ht_all_release_arr((values), __FILE__, __LINE__)
#define ht_delete(ht, key) _ht_delete((ht), (key), __FILE__, __LINE__)


/*
 * key_type is based on uintptr_t to uniformly handle both integer and pointer values.
 * It is not guaranteed to always represent a pointer, so validation is required before
 * using it as such.
 */
typedef uintptr_t key_type;
#define KEY_TYPE_MAX UINTPTR_MAX


typedef struct Entry {
	key_type key;
	void* value;
	struct Entry* next;
} Entry;


typedef struct {
	Entry** buckets;
	size_t size;     /* number of buckets */
	size_t count;    /* number of elements */
} HashTable;


/*
 * _ht_create
 * @param size: initial size of the hashtable, it will automatically round up and display a message if size isn't a power of 2
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the created hashtable
 * @note: Hashtables created by this function automatically expand when the number of entries grows too large. However, for performance reasons, we recommend setting the initial size based on the expected number of entries
 */
extern HashTable* _ht_create (size_t size, const char* file, int line);

/*
 * _ht_destroy
 * @param ht: pointer to the hashtable to destroy
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function destroys the hashtable along with the stored values
 */
extern void _ht_destroy (HashTable* ht, const char* file, int line);

/*
 * _ht_destroy_without_value
 * @param ht: pointer to the hashtable to destroy
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function destroys the hashtable but does not delete the stored values
 */
extern void _ht_destroy_without_value (HashTable* ht, const char* file, int line);

/*
 * _ht_set
 * @param ht: pointer to the hashtable
 * @param key: key to set
 * @param value_data: pointer to the value data
 * @param value_size: size of the value data
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 */
extern bool _ht_set (HashTable* ht, key_type key, void* value_data, size_t value_size, const char* file, int line);

/*
 * _ht_set_raw
 * @param ht: pointer to the hashtable
 * @param key: key to set
 * @param value_data: pointer to the value data
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 * @note: The use of this function is not recommended. Since this function sets the specified pointer directly as a value without copying the data, improper use of the ht_destroy and ht_destroy_without_value functions depending on the situation may lead to memory leaks or double frees
 */
extern bool _ht_set_raw (HashTable* ht, key_type key, void* value_data, const char* file, int line);

/*
 * _ht_get
 * @param ht: pointer to the hashtable
 * @param key: key to get
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the value data, or NULL if not found or an error occurred
 */
extern void* _ht_get (HashTable* ht, key_type key, const char* file, int line);

/*
 * _ht_all_get
 * @param ht: pointer to the hashtable
 * @param out_count: pointer to store the number of values retrieved
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: array of pointers to the values, or NULL if not found or an error occurred
 * @note: if you want to release the returned array, you must always use the ht_all_release_arr function and must not use the free function
 */
extern void** _ht_all_get (HashTable* ht, size_t* out_count, const char* file, int line);

/*
 * _ht_all_release_arr
 * @param values: pointer to the array previously returned by the ht_all_get function
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 * @note: For optimal performance, it's recommended to explicitly release the array when you're done using it. However, if omitted, the memory will still be automatically freed when the program terminates, so calling this function is optional
 */
extern bool _ht_all_release_arr (void* values, const char* file, int line);

/*
 * _ht_delete
 * @param ht: pointer to the hashtable
 * @param key: key to delete
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 */
extern bool _ht_delete (HashTable* ht, key_type key, const char* file, int line);


/*
 * The following functions are not part of this library's original purpose, but we
 * ended up creating some that are generally useful during development, so we've
 * decided to make them publicly available.
 */

/*
 * ht_next_power_of_two
 * @param n: number to find the next power of two
 * @return: next power of two greater than or equal to n, returns 0 if the result would overflow size_t
 */
static inline size_t ht_next_power_of_two (size_t n) {
	if (n == 0) return 1;
	if (n > (SIZE_MAX >> 1) + 1) return 0;
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
#if SIZE_MAX > UINT32_MAX
	n |= n >> 32;
#endif
	return n + 1;
}

/*
 * ht_is_power_of_two
 * @param n: number to check
 * @return: true if n is a power of 2, false otherwise
 */
static inline bool ht_is_power_of_two (size_t n) {
	return (n != 0) && ((n & (n - 1)) == 0);
}

/*
 * wang_hash32
 * @param key: key to hash
 * @return: hashed key
 * @note: this function is a 32-bit version of Thomas Wang’s integer hash function and must not be used for security-related purposes
 */
extern uint32_t wang_hash32 (uint32_t key);

/*
 * wang_hash64
 * @param key: key to hash
 * @return: hashed key
 * @note: this function is a 64-bit version of Thomas Wang’s integer hash function and must not be used for security-related purposes
 */
extern uint64_t wang_hash64 (uint64_t key);

/*
 * bool_text
 * @param flag: a boolean value (true or false)
 * @return: a pointer to a static string representing the boolean value, "true" if flag is true, "false" if flag is false
 */
extern const char* bool_text (bool flag);


MHT_CPP_C_END


#endif
