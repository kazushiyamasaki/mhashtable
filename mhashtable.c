/*
 * mhashtable.c -- implementation part of a simple and thread-safe hashtable library
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
 */

#include "mhashtable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#if !defined (__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
	#error "This program requires C99 or higher."
#endif


#if defined (_WIN32) && (!defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600))
	#error "This program requires Windows Vista or later. Define _WIN32_WINNT accordingly."
#endif


#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunsuffixed-float-constants"  /* mhashtable自体のデバッグを行う際は必ず外すこと */
#endif

#define LOAD_FACTOR 0.75

#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic pop
#endif

#define MHT_ENTRIES_INITIAL_SIZE 256
#define MHT_ENTRIES_TRIAL 4

#define ALL_GET_ARR_INITIAL_SIZE 16


typedef enum {
	KEY_TYPE_UINT,
	KEY_TYPE_STR
} KeyType;


typedef struct MHtEntry {
	union {
		uint_keyt uint;
		str_keyt str;
	} key;
	size_t value_size;  /* raw モードで set された場合 0 */
	void* value;
	struct MHtEntry* next;
} MHtEntry;


struct MHashTable {
	MHtEntry** buckets;
	size_t size;     /* number of buckets */
	size_t count;    /* number of elements */
	KeyType key_type;
};


typedef struct {
	MHashTable* ptr;
#ifdef DEBUG
	const char* create_file;
	int create_line;
	KeyType key_type;
#endif
} MHtTrackEntry;


typedef struct {
	union {
		uint_keyt uint;
		str_keyt str;
	} key;
	KeyType key_type;
} KeyUni;


static MHashTable* mht_entries = NULL;
static MHashTable* all_get_arr_entries = NULL;


/* errno 記録時に関数名を記録する */
#ifdef THREAD_LOCAL
	THREAD_LOCAL const char* mht_errfunc = NULL;
#else
	const char* mht_errfunc = NULL;  /* 非スレッドセーフ */
#endif


#define GLOBAL_LOCK_FUNC_NAME mht_lock
#define GLOBAL_UNLOCK_FUNC_NAME mht_unlock
#define GLOBAL_LOCK_FUNC_SCOPE static

#include "global_lock.h"


uint64_t wang_hash64 (uint64_t num) {
	num = (~num) + (num << 21);             /* num = (num << 21) - num - 1; */
	num = num ^ (num >> 24);
	num = (num + (num << 3)) + (num << 8);  /* num * 265 */
	num = num ^ (num >> 14);
	num = (num + (num << 2)) + (num << 4);  /* num * 21 */
	num = num ^ (num >> 28);
	num = num + (num << 31);
	return num;
}


uint32_t wang_hash32 (uint32_t num) {
	num = (~num) + (num << 15);    /* num = (num << 15) - num - 1; */
	num = num ^ (num >> 12);
	num = num + (num << 2);
	num = num ^ (num >> 4);
	num = num * 2057;              /* num = num + (num << 3) + (num << 11); */
	num = num ^ (num >> 16);
	return num;
}


uint64_t djb2_hash64n (const char* str, size_t len) {
	const uint8_t* p = (const uint8_t*)str;
	uint64_t hash = 5381;
	for (size_t i = 0; i < len; i++) {
		if (p[i] == '\0') break;
		hash = ((hash << 5) + hash) + p[i];  /* hash * 33 + p[i] */
	}
	return hash;
}


uint32_t djb2_hash32n (const char* str, size_t len) {
	const uint8_t* p = (const uint8_t*)str;
	uint32_t hash = 5381;
	for (size_t i = 0; i < len; i++) {
		if (p[i] == '\0') break;
		hash = ((hash << 5) + hash) + p[i];  /* hash * 33 + p[i] */
	}
	return hash;
}


static size_t hash_uint_key (uint_keyt key, size_t size) {
#if SIZE_MAX > UINT32_MAX
	size_t hash = wang_hash64(key);
	return (hash ^ (hash >> 32)) & (size - 1);
#else
	size_t hash = wang_hash32(key);
	return (hash ^ (hash >> 16)) & (size - 1);
#endif
}


static size_t hash_str_key (str_keyt key, size_t size) {
	if (key.ptr == NULL || key.len == 0 || size == 0) {
		errno = EINVAL;
		mht_errfunc = "hash_str_key";
		return 0;
	}
#if SIZE_MAX > UINT32_MAX
	size_t hash = djb2_hash64n(key.ptr, key.len);
	return (hash ^ (hash >> 32)) & (size - 1);
#else
	size_t hash = djb2_hash32n(key.ptr, key.len);
	return (hash ^ (hash >> 16)) & (size - 1);
#endif
}


bool mht_str_key_is_valid (str_keyt key) {
	if (key.ptr == NULL) return false;
	if (key.ptr[0] == '\0') return false;
	if (key.len == 0) return false;

	size_t str_len = mutils_strnlen(key.ptr, key.len);
	if (str_len < key.len) return false;

	return true;
}


static bool str_key_equal (str_keyt a, str_keyt b) {
	return (a.len == b.len) && (memcmp(a.ptr, b.ptr, a.len) == 0);
}


bool mht_str_key_equal (str_keyt a, str_keyt b) {
	if (!mht_str_key_is_valid(a) || !mht_str_key_is_valid(b)) {
		errno = EINVAL;
		mht_errfunc = "mht_str_key_equal";
		return false;
	}

	return str_key_equal(a, b);
}


static MHashTable* mht_create_without_register_generic (size_t size, KeyType key_type, const char* file, int line);
static void quit (void);
static MHashTable* mht_uint_create_without_lock (size_t size, const char* file, int line);

/* 重要: この関数は必ずロックした後に呼び出す必要があります！ */
static void init (void) {
	for (size_t i = 0; i < MHT_ENTRIES_TRIAL; i++) {
		mht_entries = mht_create_without_register_generic(MHT_ENTRIES_INITIAL_SIZE, KEY_TYPE_UINT, __FILE__, __LINE__);
		if (LIKELY(mht_entries != NULL)) break;
	}
	if (UNLIKELY(mht_entries == NULL)) {
		fprintf(stderr, "Failed to initialize mhashtable library.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		mht_unlock();
		global_lock_quit();
		exit(EXIT_FAILURE);
	}

	atexit(quit);

	all_get_arr_entries = mht_uint_create_without_lock(ALL_GET_ARR_INITIAL_SIZE, __FILE__, __LINE__);
	if (UNLIKELY(all_get_arr_entries == NULL)) {
		fprintf(stderr, "Failed to prepare the hashtable that manages the array returned by the mht_all_get function.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		mht_errfunc = "init";
	}
}


static MHashTable* mht_create_without_register_generic (size_t size, KeyType key_type, const char* file, int line) {
	if (size == 0) {
		fprintf(stderr, "Hashtable size cannot be zero.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return NULL;
	}

	if (!mutils_is_power_of_two(size)) {
		size_t adjusted = mutils_next_power_of_two(size);
		printf("Hashtable size adjusted from %zu to %zu\n", size, adjusted);
		size = adjusted;
	}

	if (size > (SIZE_MAX / sizeof(MHtEntry*))) {
		fprintf(stderr, "Hashtable size is too large.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return NULL;
	}

	MHashTable* ht = calloc(1, sizeof(MHashTable));
	if (UNLIKELY(ht == NULL)) {
		fprintf(stderr, "Failed to allocate memory for hashtable.\nFile: %s   Line: %d\n", file, line);
		errno = ENOMEM;
		return NULL;
	}

	ht->buckets = calloc(size, sizeof(MHtEntry*));  /* 今後の処理のために必ず初期化が必要 */
	if (UNLIKELY(ht->buckets == NULL)) {
		fprintf(stderr, "Failed to allocate memory for hashtable buckets.\nFile: %s   Line: %d\n", file, line);
		errno = ENOMEM;

		free(ht);

		return NULL;
	}
	ht->size = size;
	ht->count = 0;
	ht->key_type = key_type;

	return ht;
}


static bool mht_uint_set_without_lock (MHashTable* ht, uint_keyt key, void* value_data, size_t value_size, const char* file, int line);

static MHashTable* mht_uint_create_without_lock (size_t size, const char* file, int line) {
	MHashTable* ht = mht_create_without_register_generic(size, KEY_TYPE_UINT, file, line);
	if (ht == NULL) {
		mht_errfunc = "_mht_uint_create";
		return NULL;
	}

	if (UNLIKELY(mht_entries == NULL)) {
		init();
	}

	MHtTrackEntry mht_entry = {
		.ptr = ht
#ifdef DEBUG
		,
		.create_file = file,
		.create_line = line,
		.key_type = KEY_TYPE_UINT
#endif
	};

	if (UNLIKELY(!mht_uint_set_without_lock(mht_entries, (uint_keyt)ht, &mht_entry, sizeof(MHtTrackEntry), file, line))) {
		fprintf(stderr, "Failed to set hashtable in hashtable entries.\nFile: %s   Line: %d\n", file, line);
		mht_errfunc = "_mht_uint_create";
	}

	return ht;
}


MHashTable* _mht_uint_create (size_t size, const char* file, int line) {
	mht_lock();
	MHashTable* ht = mht_uint_create_without_lock(size, file, line);
	mht_unlock();
	return ht;
}


static MHashTable* mht_str_create_without_lock (size_t size, const char* file, int line) {
	MHashTable* ht = mht_create_without_register_generic(size, KEY_TYPE_STR, file, line);
	if (ht == NULL) {
		mht_errfunc = "_mht_str_create";
		return NULL;
	}

	if (UNLIKELY(mht_entries == NULL)) {
		init();
	}

	MHtTrackEntry mht_entry = {
		.ptr = ht
#ifdef DEBUG
		,
		.create_file = file,
		.create_line = line
		.key_type = KEY_TYPE_STR
#endif
	};

	if (UNLIKELY(!mht_uint_set_without_lock(mht_entries, (uint_keyt)ht, &mht_entry, sizeof(MHtTrackEntry), file, line))) {
		fprintf(stderr, "Failed to set hashtable in hashtable entries.\nFile: %s   Line: %d\n", file, line);
		mht_errfunc = "_mht_str_create";
	}

	return ht;
}


MHashTable* _mht_str_create (size_t size, const char* file, int line) {
	mht_lock();
	MHashTable* ht = mht_str_create_without_lock(size, file, line);
	mht_unlock();
	return ht;
}


static void* mht_uint_get_without_lock (MHashTable* ht, uint_keyt key, const char* file, int line);

static bool mht_pre_execution_check (MHashTable* ht, const char* file, int line) {
	if (ht == NULL) {
		fprintf(stderr, "Hashtable is NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return false;
	}

	if (UNLIKELY(mht_entries == NULL)) {
		fprintf(stderr, "Hashtable entries are NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EPERM;
		return false;
	}

	if (ht != mht_entries) {
		MHtTrackEntry* mht_entry = mht_uint_get_without_lock(mht_entries, (uint_keyt)ht, file, line);
		if (mht_entry == NULL) {
			fprintf(stderr, "Hashtable does not exist in hashtable entries.\nFile: %s   Line: %d\n", file, line);
			errno = EINVAL;
			return false;
		}
	}

	return true;
}


static void mht_destroy_value_choose_delete (MHashTable* ht, bool value_delete) {
	for (size_t i = 0; i < ht->size; i++) {
		MHtEntry* entry = ht->buckets[i];
		while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
			MHtEntry* next = entry->next;

			/* value_delete が true の場合のみ、値を削除 */
			if (value_delete) free(entry->value);

			/* キーの型が文字列の場合は、キーの文字列のために確保していたメモリブロックを解放 */
			if (ht->key_type == KEY_TYPE_STR) free(entry->key.str.ptr);

			free(entry);
			entry = next;
		}
	}
	free(ht->buckets);
	free(ht);
}


static bool mht_uint_delete_without_lock (MHashTable* ht, uint_keyt key, const char* file, int line);

void _mht_destroy (MHashTable* ht, const char* file, int line) {
	mht_lock();

	if (!mht_pre_execution_check(ht, file, line)) {
		mht_errfunc = "_mht_destroy";
		mht_unlock();
		return;
	}

	mht_destroy_value_choose_delete(ht, true);

	if (ht != mht_entries)
		mht_uint_delete_without_lock(mht_entries, (uint_keyt)ht, file, line);

	mht_unlock();
}


void _mht_destroy_without_value (MHashTable* ht, const char* file, int line) {
	mht_lock();

	if (!mht_pre_execution_check(ht, file, line)) {
		mht_errfunc = "_mht_destroy_without_value";
		mht_unlock();
		return;
	}

	mht_destroy_value_choose_delete(ht, false);

	if (ht != mht_entries)
		mht_uint_delete_without_lock(mht_entries, (uint_keyt)ht, file, line);

	mht_unlock();
}


static void mht_rehash (MHashTable* ht) {
	if (ht->size > (SIZE_MAX / 2)) {
		fprintf(stderr, "Hashtable size is too large for rehashing.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		errno = EIO;
		mht_errfunc = "mht_rehash";
		return;
	}

	size_t new_size = ht->size * 2;

	if (new_size > (SIZE_MAX / sizeof(MHtEntry*))) {
		fprintf(stderr, "Hashtable size is too large for rehashing.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		errno = EIO;
		mht_errfunc = "mht_rehash";
		return;
	}

	MHtEntry** new_buckets = calloc(new_size, sizeof(MHtEntry*));  /* 今後の処理のために必ず初期化が必要 */
	if (UNLIKELY(new_buckets == NULL)) {
		fprintf(stderr, "Failed to allocate memory for rehashing.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		errno = ENOMEM;
		mht_errfunc = "mht_rehash";
		return;
	}

	for (size_t i = 0; i < ht->size; i++) {
		MHtEntry* entry = ht->buckets[i];
		while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
			MHtEntry* next = entry->next;

			size_t new_index;
			if (ht->key_type == KEY_TYPE_UINT)
				new_index = hash_uint_key(entry->key.uint, new_size);
			else  /* if (ht->key_type == KEY_TYPE_STR) */
				new_index = hash_str_key(entry->key.str, new_size);

			entry->next = new_buckets[new_index];
			new_buckets[new_index] = entry;
			entry = next;
		}
	}

	free(ht->buckets);
	ht->buckets = new_buckets;
	ht->size = new_size;
}


/* value_size が 0 のときに raw モードになる。ロック内で使用すること。 */
static bool mht_set_generic (MHashTable* ht, KeyUni key, void* value_data, size_t value_size, const char* file, int line) {
	if (!mht_pre_execution_check(ht, file, line))
		return false;

	if (value_data == NULL) {
		fprintf(stderr, "Value pointer is NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return false;
	}

	if (ht->key_type != key.key_type) {
		fprintf(stderr, "Key type mismatch in hashtable.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return false;
	}

#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunsuffixed-float-constants"  /* mhashtable自体のデバッグを行う際は必ず外すこと */
#endif

	if (UNLIKELY(((double)ht->count / (double)ht->size) > LOAD_FACTOR))
		mht_rehash(ht);

#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic pop
#endif

	size_t index;
	if (ht->key_type == KEY_TYPE_UINT)
		index = hash_uint_key(key.key.uint, ht->size);
	else  /* if (ht->key_type == KEY_TYPE_STR) */
		index = hash_str_key(key.key.str, ht->size);

	MHtEntry* entry = ht->buckets[index];

	/* 既存キーを更新（上書き） */
	while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
		if ((ht->key_type == KEY_TYPE_UINT && entry->key.uint == key.key.uint) ||
			(ht->key_type == KEY_TYPE_STR && str_key_equal(entry->key.str, key.key.str))) {
			if (value_size != 0) {
				void* new_value = calloc(1, value_size);
				if (UNLIKELY(new_value == NULL)) return false;

				free(entry->value);
				entry->value = new_value;
				memcpy(entry->value, value_data, value_size);
			} else {
				free(entry->value);
				entry->value = value_data;
			}
			entry->value_size = value_size;
			return true;
		}
		entry = entry->next;
	}

	/* 新規追加 */
	MHtEntry* new_entry = calloc(1, sizeof(MHtEntry));
	if (UNLIKELY(new_entry == NULL)) return false;

	if (ht->key_type == KEY_TYPE_UINT) {
		new_entry->key.uint = key.key.uint;
	} else {  /* if (ht->key_type == KEY_TYPE_STR) */
		char* key_str = mutils_strndup(key.key.str.ptr, key.key.str.len);
		if (UNLIKELY(key_str == NULL)) {
			free(new_entry);
			errno = ENOMEM;
			return false;
		}

		new_entry->key.str.ptr = key_str;
		new_entry->key.str.len = key.key.str.len;
	}

	if (value_size != 0) {
		new_entry->value = calloc(1, value_size);
		if (UNLIKELY(new_entry->value == NULL)) {
			if (ht->key_type == KEY_TYPE_STR) free(new_entry->key.str.ptr);
			free(new_entry);
			errno = ENOMEM;
			return false;
		}
		memcpy(new_entry->value, value_data, value_size);
	} else {
		new_entry->value = value_data;
	}
	new_entry->value_size = value_size;

	new_entry->next = ht->buckets[index];
	ht->buckets[index] = new_entry;
	ht->count++;
	return true;
}


static bool mht_uint_set_raw_without_lock (MHashTable* ht, uint_keyt key, void* value_data, const char* file, int line) {
	KeyUni key_uni = {
		.key.uint = key,
		.key_type = KEY_TYPE_UINT
	};

	/* value_data に 0 を渡して raw モードに */
	bool result = mht_set_generic(ht, key_uni, value_data, 0, file, line);
	if (!result) mht_errfunc = "_mht_uint_set_raw";
	return result;
}


bool _mht_uint_set_raw (MHashTable* ht, uint_keyt key, void* value_data, const char* file, int line) {
	mht_lock();
	bool result = mht_uint_set_raw_without_lock(ht, key, value_data, file, line);
	mht_unlock();
	return result;
}


static bool mht_uint_set_without_lock (MHashTable* ht, uint_keyt key, void* value_data, size_t value_size, const char* file, int line) {
	/* mht_set_generic の value_data に 0 を渡すと raw モードになってしまうので、先に排除しておく */
	if (value_size == 0) {
		fprintf(stderr, "Value size is zero.\nFile: %s   Line: %d\n", file, line);
		return false;
	}

	KeyUni key_uni = {
		.key.uint = key,
		.key_type = KEY_TYPE_UINT
	};

	bool result = mht_set_generic(ht, key_uni, value_data, value_size, file, line);
	if (!result) mht_errfunc = "_mht_uint_set";
	return result;
}


bool _mht_uint_set (MHashTable* ht, uint_keyt key, void* value_data, size_t value_size, const char* file, int line) {
	mht_lock();
	bool result = mht_uint_set_without_lock(ht, key, value_data, value_size, file, line);
	mht_unlock();
	return result;
}


static bool mht_str_set_raw_without_lock (MHashTable* ht, str_keyt key, void* value_data, const char* file, int line) {
	if (!mht_str_key_is_valid(key)) {
		fprintf(stderr, "Invalid string key.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		mht_errfunc = "_mht_str_set_raw";
		return false;
	}

	KeyUni key_uni = {
		.key.str = key,
		.key_type = KEY_TYPE_STR
	};

	/* value_data に 0 を渡して raw モードに */
	bool result = mht_set_generic(ht, key_uni, value_data, 0, file, line);
	if (!result) mht_errfunc = "_mht_str_set_raw";
	return result;
}


bool _mht_str_set_raw (MHashTable* ht, str_keyt key, void* value_data, const char* file, int line) {
	mht_lock();
	bool result = mht_str_set_raw_without_lock(ht, key, value_data, file, line);
	mht_unlock();
	return result;
}


static bool mht_str_set_without_lock (MHashTable* ht, str_keyt key, void* value_data, size_t value_size, const char* file, int line) {
	/* mht_set_generic の value_data に 0 を渡すと raw モードになってしまうので、先に排除しておく */
	if (value_size == 0) {
		fprintf(stderr, "Value size is zero.\nFile: %s   Line: %d\n", file, line);
		return false;
	}

	if (!mht_str_key_is_valid(key)) {
		fprintf(stderr, "Invalid string key.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		mht_errfunc = "_mht_str_set";
		return false;
	}

	KeyUni key_uni = {
		.key.str = key,
		.key_type = KEY_TYPE_STR
	};

	bool result = mht_set_generic(ht, key_uni, value_data, value_size, file, line);
	if (!result) mht_errfunc = "_mht_str_set";
	return result;
}


bool _mht_str_set (MHashTable* ht, str_keyt key, void* value_data, size_t value_size, const char* file, int line) {
	mht_lock();
	bool result = mht_str_set_without_lock(ht, key, value_data, value_size, file, line);
	mht_unlock();
	return result;
}


static void* mht_get_without_lock_generic (MHashTable* ht, KeyUni key, const char* file, int line) {
	if (!mht_pre_execution_check(ht, file, line)) return NULL;

	if (ht->key_type != key.key_type) {
		fprintf(stderr, "Key type mismatch in hashtable.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return NULL;
	}

	size_t index;
	if (ht->key_type == KEY_TYPE_UINT)
		index = hash_uint_key(key.key.uint, ht->size);
	else  /* if (ht->key_type == KEY_TYPE_STR) */
		index = hash_str_key(key.key.str, ht->size);

	MHtEntry* entry = ht->buckets[index];

	while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
		if ((ht->key_type == KEY_TYPE_UINT && entry->key.uint == key.key.uint) ||
			(ht->key_type == KEY_TYPE_STR && str_key_equal(entry->key.str, key.key.str)))
			return entry->value;
		entry = entry->next;
	}

	fprintf(stderr, "Key not found in hashtable.\nFile: %s   Line: %d\n", file, line);
	errno = EINVAL;
	return NULL;
}


static void* mht_uint_get_without_lock (MHashTable* ht, uint_keyt key, const char* file, int line) {
	KeyUni key_uni = {
		.key.uint = key,
		.key_type = KEY_TYPE_UINT
	};

	void* result = mht_get_without_lock_generic(ht, key_uni, file, line);
	if (result == NULL) mht_errfunc = "_mht_uint_get";

	return result;
}


void* _mht_uint_get (MHashTable* ht, uint_keyt key, const char* file, int line) {
	mht_lock();
	void* result = mht_uint_get_without_lock(ht, key, file, line);
	mht_unlock();
	return result;
}


static void* mht_str_get_without_lock (MHashTable* ht, str_keyt key, const char* file, int line) {
	if (!mht_str_key_is_valid(key)) {
		fprintf(stderr, "Invalid string key.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		mht_errfunc = "_mht_str_get";
		return false;
	}

	KeyUni key_uni = {
		.key.str = key,
		.key_type = KEY_TYPE_STR
	};

	void* result = mht_get_without_lock_generic(ht, key_uni, file, line);
	if (result == NULL) mht_errfunc = "_mht_str_get";

	return result;
}


void* _mht_str_get (MHashTable* ht, str_keyt key, const char* file, int line) {
	mht_lock();
	void* result = mht_str_get_without_lock(ht, key, file, line);
	mht_unlock();
	return result;
}


void** _mht_all_get (MHashTable* ht, size_t* out_count, const char* file, int line) {
	mht_lock();

	if (out_count == NULL) {
		fprintf(stderr, "Output count pointer is NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		mht_errfunc = "_mht_all_get";
		return NULL;
	}

	if (!mht_pre_execution_check(ht, file, line)) {
		mht_errfunc = "_mht_all_get";
		mht_unlock();
		return NULL;
	}

	if (ht->count > (SIZE_MAX / sizeof(void*))) {
		fprintf(stderr, "Hashtable count is too large for all_get.\nFile: %s   Line: %d\n", file, line);
		errno = EIO;
		mht_errfunc = "_mht_all_get";
		return NULL;
	}

	void** values = calloc(ht->count, sizeof(void*));
	if (UNLIKELY(values == NULL)) {
		errno = ENOMEM;
		mht_errfunc = "_mht_all_get";
		mht_unlock();
		return NULL;
	}

	size_t idx = 0;
	for (size_t i = 0; i < ht->size; ++i) {
		MHtEntry* entry = ht->buckets[i];
		while (entry) {
			values[idx++] = entry->value;
			entry = entry->next;
		}
	}

	mht_uint_set_raw_without_lock(all_get_arr_entries, (uint_keyt)values, values, file, line);

	*out_count = idx;  /* 正常なら ht->count と等しい */
	mht_unlock();
	return values;
}


bool _mht_all_release_arr (void* values, const char* file, int line) {
	return _mht_uint_delete(all_get_arr_entries, (uint_keyt)values, file, line);
}


static bool mht_delete_without_lock_generic (MHashTable* ht, KeyUni key, const char* file, int line) {
	if (!mht_pre_execution_check(ht, file, line)) return false;

	if (ht->key_type != key.key_type) {
		fprintf(stderr, "Key type mismatch in hashtable.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return NULL;
	}

	size_t index;
	if (ht->key_type == KEY_TYPE_UINT)
		index = hash_uint_key(key.key.uint, ht->size);
	else  /* if (ht->key_type == KEY_TYPE_STR) */
		index = hash_str_key(key.key.str, ht->size);

	MHtEntry* prev = NULL;
	MHtEntry* entry = ht->buckets[index];

	while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
		if ((ht->key_type == KEY_TYPE_UINT && entry->key.uint == key.key.uint) ||
			(ht->key_type == KEY_TYPE_STR && str_key_equal(entry->key.str, key.key.str))) {
			if (prev)
				prev->next = entry->next;
			else
				ht->buckets[index] = entry->next;

			free(entry->value);
			free(entry);
			ht->count--;
			return true;
		}
		prev = entry;
		entry = entry->next;
	}

	fprintf(stderr, "Key not found in hashtable.\nFile: %s   Line: %d\n", file, line);
	errno = EINVAL;
	return false;
}


static bool mht_uint_delete_without_lock (MHashTable* ht, uint_keyt key, const char* file, int line) {
	KeyUni key_uni = {
		.key.uint = key,
		.key_type = KEY_TYPE_UINT
	};

	bool result = mht_delete_without_lock_generic(ht, key_uni, file, line);
	if (result == false) mht_errfunc = "_mht_uint_delete";

	return result;
}


bool _mht_uint_delete (MHashTable* ht, uint_keyt key, const char* file, int line) {
	mht_lock();
	bool result = mht_uint_delete_without_lock(ht, key, file, line);
	mht_unlock();
	return result;
}


static bool mht_str_delete_without_lock (MHashTable* ht, str_keyt key, const char* file, int line) {
	if (!mht_str_key_is_valid(key)) {
		fprintf(stderr, "Invalid string key.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		mht_errfunc = "_mht_str_delete";
		return false;
	}

	KeyUni key_uni = {
		.key.str = key,
		.key_type = KEY_TYPE_STR
	};

	bool result = mht_delete_without_lock_generic(ht, key_uni, file, line);
	if (result == false) mht_errfunc = "_mht_str_delete";

	return result;
}


bool _mht_str_delete (MHashTable* ht, str_keyt key, const char* file, int line) {
	mht_lock();
	bool result = mht_str_delete_without_lock(ht, key, file, line);
	mht_unlock();
	return result;
}


static void quit (void) {
	_mht_destroy(all_get_arr_entries, __FILE__, __LINE__);
	all_get_arr_entries = NULL;

	for (size_t i = 0; i < mht_entries->size; i++) {
		MHtEntry* entry = mht_entries->buckets[i];
		while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
			MHtEntry* next = entry->next;
#ifdef DEBUG
			if (((MHtTrackEntry*)entry->value)->key_type == KEY_TYPE_UINT)
				fprintf(stderr, "\nHashtable not destroyed!\nKey type: %s\nFile: %s   Line: %d\n", "uint", ((MHtTrackEntry*)entry->value)->create_file, ((MHtTrackEntry*)entry->value)->create_line);
			else  /* if (((MHtTrackEntry*)entry->value)->key_type == KEY_TYPE_STR) */
				fprintf(stderr, "\nHashtable not destroyed!\nKey type: %s\nFile: %s   Line: %d\n", "str", ((MHtTrackEntry*)entry->value)->create_file, ((MHtTrackEntry*)entry->value)->create_line);
#endif
			_mht_destroy(((MHtTrackEntry*)entry->value)->ptr, __FILE__, __LINE__);
			entry = next;
		}
	}
	_mht_destroy(mht_entries, __FILE__, __LINE__);
	mht_entries = NULL;

	global_lock_quit();
}
