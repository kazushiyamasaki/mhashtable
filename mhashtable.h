/*
 * mhashtable.h -- interface of a simple and thread-safe hashtable library
 * version 0.9.5, June 22, 2025
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
 * do not use mht_destroy to destroy the outer hashtable. Instead, use
 * mht_destroy_without_value.
 * Conversely, do not use mht_destroy_without_value to destroy the inner (pointed-to)
 * hashtable use mht_destroy instead.
 * However, if you store a pointer to a hashtable inside a struct, and that struct
 * is stored as a value in another hashtable, you must use mht_destroy to properly
 * destroy both hashtables.
 *
 * The functions mht_uint_create, mht_destroy, mht_destroy_without_value, mht_uint_set, mht_uint_set_raw,
 * mht_uint_get, mht_all_get, and mht_uint_delete are also implemented as macros and cannot be
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


#include "mutils.h"


MUTILS_CPP_C_BEGIN


/*
 * The functions replaced by the following macros are specific to this library.
 * It is recommended not to remove them unless a conflict occurs.
 */
#define mht_uint_create(size) _mht_uint_create((size), __FILE__, __LINE__)
#define mht_str_create(size) _mht_str_create((size), __FILE__, __LINE__)
#define mht_destroy(ht) _mht_destroy((ht), __FILE__, __LINE__)
#define mht_destroy_without_value(ht) _mht_destroy_without_value((ht), __FILE__, __LINE__)
#define mht_uint_set(ht, key, value_data, value_size) _mht_uint_set((ht), (key), (value_data), (value_size), __FILE__, __LINE__)
#define mht_str_set(ht, key, value_data, value_size) _mht_str_set((ht), (key), (value_data), (value_size), __FILE__, __LINE__)
#define mht_uint_get(ht, key) _mht_uint_get((ht), (key), __FILE__, __LINE__)
#define mht_str_get(ht, key) _mht_str_get((ht), (key), __FILE__, __LINE__)
#define mht_all_get(ht, out_count) _mht_all_get((ht), (out_count), __FILE__, __LINE__)
#define mht_all_release_arr(values) _mht_all_release_arr((values), __FILE__, __LINE__)
#define mht_uint_delete(ht, key) _mht_uint_delete((ht), (key), __FILE__, __LINE__)
#define mht_str_delete(ht, key) _mht_str_delete((ht), (key), __FILE__, __LINE__)
#define mht_uint_set_raw(ht, key, value_data) _mht_uint_set_raw((ht), (key), (value_data), __FILE__, __LINE__)
#define mht_str_set_raw(ht, key, value_data) _mht_str_set_raw((ht), (key), (value_data), __FILE__, __LINE__)


/*
 * uint_keyt is based on uintptr_t to uniformly handle both integer and pointer values.
 * It is not guaranteed to always represent a pointer, so validation is required before
 * using it as such.
 */
typedef uintptr_t uint_keyt;

#define UINT_KEY_MAX UINTPTR_MAX


/*
 * The str_keyt type is a structure consisting of a pointer to a string and its length.
 * It is the only accepted key type for use with the mht_str_* family of functions.
 * A str_keyt with a NULL pointer, a string containing only a null terminator, or a
 * length of zero is considered invalid.
 */
typedef struct {
	char* ptr;
	size_t len;
} str_keyt;

#define STR_KEYT_INVALID (str_keyt){ NULL, 0 }

/*
 * When using string literals, the STR_KEY_LITERAL("some string") macro allows you to
 * create a str_keyt without explicitly constructing the structure.
 * However, note that this macro must only be used with string literals, and using it
 * with other types of strings is not safe.
 */
#define STR_KEY_LITERAL(s) (str_keyt){ .ptr = (s), .len = sizeof(s) - 1 }


/* MHashTable* is used as a handle to a hashtable. */
typedef struct MHashTable MHashTable;


/*
 * mht_errfunc is a global variable that stores the name of the function
 * where the most recent error occurred within this library.
 *
 * It is set to NULL when no error has occurred.
 * This variable is used to provide more informative error diagnostics,
 * especially in combination with errno.
 *
 * It is recommended to check this variable and errno after calling
 * any library function that may fail.
 */
#ifdef THREAD_LOCAL
	extern THREAD_LOCAL const char* mht_errfunc;
#else
	extern const char* mht_errfunc;
#endif


/*
 * _mht_uint_create
 * @param size: initial size of the hashtable, it will automatically round up and display a message if size isn't a power of 2
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the created hashtable
 * @note: This function creates a hashtable that uses unsigned integer keys. (Pointers can also be used as keys by casting them to uint_keyt.) The hashtable automatically expands when the number of entries becomes too large. However, for performance reasons, it is recommended to set the initial size based on the expected number of entries
 */
extern MHashTable* _mht_uint_create (size_t size, const char* file, int line);

/*
 * _mht_str_create
 * @param size: initial size of the hashtable, it will automatically round up and display a message if size isn't a power of 2
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the created hashtable
 * @note: This function creates a hashtable that uses string keys. The keys must be valid str_keyt types. The hashtable automatically expands when the number of entries becomes too large. However, for performance reasons, it is recommended to set the initial size based on the expected number of entries
 */
extern MHashTable* _mht_str_create (size_t size, const char* file, int line);

/*
 * _mht_destroy
 * @param ht: pointer to the hashtable to destroy
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function destroys the hashtable along with the stored values
 */
extern void _mht_destroy (MHashTable* ht, const char* file, int line);

/*
 * _mht_destroy_without_value
 * @param ht: pointer to the hashtable to destroy
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @note: this function destroys the hashtable but does not delete the stored values
 */
extern void _mht_destroy_without_value (MHashTable* ht, const char* file, int line);

/*
 * _mht_uint_set
 * @param ht: pointer to the hashtable
 * @param key: key to set
 * @param value_data: pointer to the value data
 * @param value_size: size of the value data
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 */
extern bool _mht_uint_set (MHashTable* ht, uint_keyt key, void* value_data, size_t value_size, const char* file, int line);

/*
 * _mht_str_set
 * @param ht: pointer to the hashtable
 * @param key: key to set
 * @param value_data: pointer to the value data
 * @param value_size: size of the value data
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 */
extern bool _mht_str_set (MHashTable* ht, str_keyt key, void* value_data, size_t value_size, const char* file, int line);

/*
 * _mht_uint_get
 * @param ht: pointer to the hashtable
 * @param key: key to get
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the value data, or NULL if not found or an error occurred
 */
extern void* _mht_uint_get (MHashTable* ht, uint_keyt key, const char* file, int line);

/*
 * _mht_str_get
 * @param ht: pointer to the hashtable
 * @param key: key to get
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: pointer to the value data, or NULL if not found or an error occurred
 */
extern void* _mht_str_get (MHashTable* ht, str_keyt key, const char* file, int line);

/*
 * _mht_all_get
 * @param ht: pointer to the hashtable
 * @param out_count: pointer to store the number of values retrieved
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: array of pointers to the values, or NULL if not found or an error occurred
 * @note: if you want to release the returned array, you must always use the mht_all_release_arr function and must not use the free function
 */
extern void** _mht_all_get (MHashTable* ht, size_t* out_count, const char* file, int line);

/*
 * _mht_all_release_arr
 * @param values: pointer to the array previously returned by the mht_all_get function
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 * @note: For optimal performance, it's recommended to explicitly release the array when you're done using it. However, if omitted, the memory will still be automatically freed when the program terminates, so calling this function is optional
 */
extern bool _mht_all_release_arr (void* values, const char* file, int line);

/*
 * _mht_uint_delete
 * @param ht: pointer to the hashtable
 * @param key: key to delete
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 */
extern bool _mht_uint_delete (MHashTable* ht, uint_keyt key, const char* file, int line);

/*
 * _mht_str_delete
 * @param ht: pointer to the hashtable
 * @param key: key to delete
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 */
extern bool _mht_str_delete (MHashTable* ht, str_keyt key, const char* file, int line);

/*
 * mht_str_key_equal
 * @param a: first key to compare
 * @param b: second key to compare
 * @return: true if both keys are valid and equal, false otherwise
 */
extern bool mht_str_key_equal (str_keyt a, str_keyt b);

/*
 * mht_str_key_is_valid
 * @param key: key to validate
 * @return: true if the key is valid, false otherwise
 * @note: a valid str_keyt must have a non-NULL pointer, a non-zero length, and the string must not be empty (i.e., it must not consist solely of a null terminator)
 */
extern bool mht_str_key_is_valid (str_keyt key);


/*
 * _mht_uint_set_raw
 * @param ht: pointer to the hashtable
 * @param key: key to set
 * @param value_data: pointer to the value data
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 * @note: The use of this function is not recommended. Since this function sets the specified pointer directly as a value without copying the data, improper use of the mht_destroy and mht_destroy_without_value functions depending on the situation may lead to memory leaks or double frees
 */
extern bool _mht_uint_set_raw (MHashTable* ht, uint_keyt key, void* value_data, const char* file, int line);

/*
 * _mht_str_set_raw
 * @param ht: pointer to the hashtable
 * @param key: key to set
 * @param value_data: pointer to the value data
 * @param file: name of the calling file, usually specified with __FILE__
 * @param line: line number of the caller, usually specified with __LINE__
 * @return: true if successful, false otherwise
 * @note: The use of this function is not recommended. Since this function sets the specified pointer directly as a value without copying the data, improper use of the mht_destroy and mht_destroy_without_value functions depending on the situation may lead to memory leaks or double frees
 */
extern bool _mht_str_set_raw (MHashTable* ht, str_keyt key, void* value_data, const char* file, int line);


/*
 * The following functions are not part of this library's original purpose, but we
 * ended up creating some that are generally useful during development, so we've
 * decided to make them publicly available.
 */

/*
 * wang_hash32
 * @param num: the input unsigned integer to hash
 * @return: the hash value as a 32-bit unsigned integer
 * @note: this is a 32-bit version of Thomas Wang’s integer hash function and must not be used for cryptographic purposes
 */
extern uint32_t wang_hash32 (uint32_t num);

/*
 * wang_hash64
 * @param num: the input unsigned integer to hash
 * @return: the hash value as a 64-bit unsigned integer
 * @note: this is a 64-bit version of Thomas Wang’s integer hash function and must not be used for cryptographic purposes
 */
extern uint64_t wang_hash64 (uint64_t num);

/*
 * djb2_hash32n
 * @param str: the input string to hash
 * @param len: the length of the input string
 * @return: the hash value as a 32-bit unsigned integer
 * @note: this is a 32-bit version of the djb2 hash function and must not be used for cryptographic purposes
 */
extern uint32_t djb2_hash32n (const char* str, size_t len);

/*
 * djb2_hash64n
 * @param str: the input string to hash
 * @param len: the length of the input string
 * @return: the hash value as a 64-bit unsigned integer
 * @note: this is a 64-bit version of the djb2 hash function and must not be used for cryptographic purposes
 */
extern uint64_t djb2_hash64n (const char* str, size_t len);


MUTILS_CPP_C_END


#endif
