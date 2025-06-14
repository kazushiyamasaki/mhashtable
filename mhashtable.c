/*
 * mhashtable.c -- implementation part of a simple and thread-safe hashtable library
 * version 0.9.2, June 14, 2025
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


#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunsuffixed-float-constants"  /* mhashtable自体のデバッグを行う際は必ず外すこと */
#endif

#define LOAD_FACTOR 0.75

#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic pop
#endif

#define HT_ENTRIES_INITIAL_SIZE 256
#define HT_ENTRIES_TRIAL 4

#define ALL_GET_ARR_INITIAL_SIZE 16


typedef struct {
	HashTable* ptr;
#ifdef DEBUG
	const char* create_file;
	int create_line;
#endif
} HtTrackEntry;


static HashTable* ht_entries = NULL;
static HashTable* all_get_arr_entries = NULL;


/* errno 記録時に関数名を記録する */
const char* ht_errfunc = NULL;  /* 非スレッドセーフ */


#define GLOBAL_LOCK_FUNC_NAME ht_lock
#define GLOBAL_UNLOCK_FUNC_NAME ht_unlock
#define GLOBAL_LOCK_FUNC_SCOPE static

#include "global_lock.h"


const char* bool_text (bool flag) {
	return flag ? "true" : "false";
}


uint64_t wang_hash64 (uint64_t key) {
	key = (~key) + (key << 21);             /* key = (key << 21) - key - 1; */
	key = key ^ (key >> 24);
	key = (key + (key << 3)) + (key << 8);  /* key * 265 */
	key = key ^ (key >> 14);
	key = (key + (key << 2)) + (key << 4);  /* key * 21 */
	key = key ^ (key >> 28);
	key = key + (key << 31);
	return key;
}


uint32_t wang_hash32 (uint32_t key) {
	key = (~key) + (key << 15);    /* key = (key << 15) - key - 1; */
	key = key ^ (key >> 12);
	key = key + (key << 2);
	key = key ^ (key >> 4);
	key = key * 2057;              /* key = key + (key << 3) + (key << 11); */
	key = key ^ (key >> 16);
	return key;
}


static key_type wang_hash (key_type key) {
#if KEY_TYPE_MAX > UINT32_MAX
	return wang_hash64(key);
#else
	return wang_hash32(key);
#endif
}


static size_t hash_key (key_type key, size_t size) {
	key_type hash = wang_hash(key);
#if KEY_TYPE_MAX > UINT32_MAX
	return (hash ^ (hash >> 32)) & (size - 1);
#else
	return (hash ^ (hash >> 16)) & (size - 1);
#endif
}


static HashTable* ht_create_without_register (size_t size, const char* file, int line);
static void quit (void);
static HashTable* ht_create_without_lock (size_t size, const char* file, int line);

/* 重要: この関数は必ずロックした後に呼び出す必要があります！ */
static void init (void) {
	for (size_t i = 0; i < HT_ENTRIES_TRIAL; i++) {
		ht_entries = ht_create_without_register(HT_ENTRIES_INITIAL_SIZE, file, line);
		if (LIKELY(ht_entries != NULL)) break;
	}
	if (UNLIKELY(ht_entries == NULL)) {
		fprintf(stderr, "Failed to initialize hashtable library.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		ht_unlock();
		global_lock_quit();
		exit(EXIT_FAILURE);
	}

	atexit(quit);

	all_get_arr_entries = ht_create_without_lock(ALL_GET_ARR_INITIAL_SIZE, __FILE__, __LINE__);
	if (UNLIKELY(all_get_arr_entries == NULL)) {
		fprintf(stderr, "Failed to prepare the hashtable that manages the array returned by the ht_all_get function.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		ht_errfunc = "init";
	}
}


static HashTable* ht_create_without_register (size_t size, const char* file, int line) {
	if (size == 0) return NULL;

	if (!ht_is_power_of_two(size)) {
		size_t adjusted = ht_next_power_of_two(size);
		printf("Hashtable size adjusted from %zu to %zu\n", size, adjusted);
		size = adjusted;
	}

	HashTable* ht = calloc(1, sizeof(HashTable));
	if (UNLIKELY(ht == NULL)) {
		fprintf(stderr, "Failed to allocate memory for hashtable.\nFile: %s   Line: %d\n", file, line);
		errno = ENOMEM;
		ht_errfunc = "_ht_create";
		return NULL;
	}

	ht->buckets = calloc(size, sizeof(Entry*));  /* 今後の処理のために必ず初期化が必要 */
	if (UNLIKELY(ht->buckets == NULL)) {
		fprintf(stderr, "Failed to allocate memory for hashtable buckets.\nFile: %s   Line: %d\n", file, line);
		errno = ENOMEM;
		ht_errfunc = "_ht_create";

		free(ht);

		return NULL;
	}
	ht->size = size;
	ht->count = 0;

	return ht;
}


static bool ht_set_without_lock (HashTable* ht, key_type key, void* value_data, size_t value_size, const char* file, int line);

static HashTable* ht_create_without_lock (size_t size, const char* file, int line) {
	HashTable* ht = ht_create_without_register(size, file, line);

	if (UNLIKELY(ht_entries == NULL)) {
		init();
	}

	HtTrackEntry ht_entry = {
		.ptr = ht
#ifdef DEBUG
		,
		.create_file = file,
		.create_line = line
#endif
	};

	if (UNLIKELY(!ht_set_without_lock(ht_entries, (key_type)ht, &ht_entry, sizeof(HtTrackEntry), file, line))) {
		fprintf(stderr, "Failed to set hashtable in hashtable entries.\nFile: %s   Line: %d\n", file, line);
		ht_errfunc = "_ht_create";
	}

	return ht;
}


HashTable* _ht_create (size_t size, const char* file, int line) {
	ht_lock();
	HashTable* ht = ht_create_without_lock(size, file, line);
	ht_unlock();
	return ht;
}


static void* ht_get_without_lock (HashTable* ht, key_type key, const char* file, int line);

static bool ht_pre_execution_check (HashTable* ht, const char* file, int line) {
	if (ht == NULL) {
		fprintf(stderr, "Hashtable is NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return false;
	}

	if (UNLIKELY(ht_entries == NULL)) {
		fprintf(stderr, "Hashtable entries are NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EPERM;
		return false;
	}

	if (ht != ht_entries) {
		HtTrackEntry* ht_entry = ht_get_without_lock(ht_entries, (key_type)ht, file, line);
		if (ht_entry == NULL) {
			fprintf(stderr, "Hashtable does not exist in hashtable entries.\nFile: %s   Line: %d\n", file, line);
			errno = EINVAL;
			return false;
		}
	}

	return true;
}


static void ht_destroy_value_choose_delete (HashTable* ht, bool value_delete) {
	for (size_t i = 0; i < ht->size; i++) {
		Entry* entry = ht->buckets[i];
		while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
			Entry* next = entry->next;
			if (value_delete) free(entry->value);
			free(entry);
			entry = next;
		}
	}
	free(ht->buckets);
	free(ht);
}


static bool ht_delete_without_lock (HashTable* ht, key_type key, const char* file, int line);

void _ht_destroy (HashTable* ht, const char* file, int line) {
	ht_lock();

	if (!ht_pre_execution_check(ht, file, line)) {
		ht_errfunc = "_ht_destroy";
		ht_unlock();
		return;
	}

	ht_destroy_value_choose_delete(ht, true);

	if (ht != ht_entries)
		ht_delete_without_lock(ht_entries, (key_type)ht, file, line);

	ht_unlock();
}


void _ht_destroy_without_value (HashTable* ht, const char* file, int line) {
	ht_lock();

	if (!ht_pre_execution_check(ht, file, line)) {
		ht_errfunc = "_ht_destroy_without_value";
		ht_unlock();
		return;
	}

	ht_destroy_value_choose_delete(ht, false);

	if (ht != ht_entries)
		ht_delete_without_lock(ht_entries, (key_type)ht, file, line);

	ht_unlock();
}


static void ht_rehash (HashTable* ht) {
	size_t new_size = ht->size * 2;
	Entry** new_buckets = calloc(new_size, sizeof(Entry*));  /* 今後の処理のために必ず初期化が必要 */
	if (UNLIKELY(new_buckets == NULL)) {
		fprintf(stderr, "Failed to allocate memory for rehashing.\nFile: %s   Line: %d\n", __FILE__, __LINE__);
		errno = ENOMEM;
		ht_errfunc = "ht_rehash";
		return;
	}

	for (size_t i = 0; i < ht->size; i++) {
		Entry* entry = ht->buckets[i];
		while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
			Entry* next = entry->next;
			size_t new_index = hash_key(entry->key, new_size);
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
static bool ht_set_generic (HashTable* ht, key_type key, void* value_data, size_t value_size, const char* file, int line) {
	if (!ht_pre_execution_check(ht, file, line))
		return false;

	if (value_data == NULL) {
		fprintf(stderr, "Value pointer is NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		return false;
	}

#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunsuffixed-float-constants"  /* mhashtable自体のデバッグを行う際は必ず外すこと */
#endif

	if (UNLIKELY(((double)ht->count / (double)ht->size) > LOAD_FACTOR))
		ht_rehash(ht);

#if defined (__GNUC__) && !defined (__clang__)
	#pragma GCC diagnostic pop
#endif

	size_t index = hash_key(key, ht->size);
	Entry* entry = ht->buckets[index];

	/* 既存キーを更新（上書き） */
	while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
		if (entry->key == key) {
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
			return true;
		}
		entry = entry->next;
	}

	/* 新規追加 */
	Entry* new_entry = calloc(1, sizeof(Entry));
	if (UNLIKELY(new_entry == NULL)) return false;

	new_entry->key = key;
	if (value_size != 0) {
		new_entry->value = calloc(1, value_size);
		if (UNLIKELY(new_entry->value == NULL)) {
			free(new_entry);
			errno = ENOMEM;
			return false;
		}
		memcpy(new_entry->value, value_data, value_size);
	} else {
		new_entry->value = value_data;
	}

	new_entry->next = ht->buckets[index];
	ht->buckets[index] = new_entry;
	ht->count++;
	return true;
}


static bool ht_set_raw_without_lock (HashTable* ht, key_type key, void* value_data, const char* file, int line) {
	/* value_data に 0 を渡して raw モードに */
	bool result = ht_set_generic(ht, key, value_data, 0, file, line);
	if (!result) ht_errfunc = "_ht_set_raw";
	return result;
}


bool _ht_set_raw (HashTable* ht, key_type key, void* value_data, const char* file, int line) {
	ht_lock();
	bool result = ht_set_raw_without_lock(ht, key, value_data, file, line);
	ht_unlock();
	return result;
}


static bool ht_set_without_lock (HashTable* ht, key_type key, void* value_data, size_t value_size, const char* file, int line) {
	/* ht_set_generic の value_data に 0 を渡すと raw モードになってしまうので、先に排除しておく */
	if (value_size == 0) {
		fprintf(stderr, "Value size is zero.\nFile: %s   Line: %d\n", file, line);
		return false;
	}

	bool result = ht_set_generic(ht, key, value_data, value_size, file, line);
	if (!result) ht_errfunc = "_ht_set";
	return result;
}


bool _ht_set (HashTable* ht, key_type key, void* value_data, size_t value_size, const char* file, int line) {
	ht_lock();
	bool result = ht_set_without_lock(ht, key, value_data, value_size, file, line);
	ht_unlock();
	return result;
}


static void* ht_get_without_lock (HashTable* ht, key_type key, const char* file, int line) {
	if (!ht_pre_execution_check(ht, file, line)) {
		ht_errfunc = "_ht_get";
		return NULL;
	}

	size_t index = hash_key(key, ht->size);
	Entry* entry = ht->buckets[index];

	while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
		if (entry->key == key)
			return entry->value;
		entry = entry->next;
	}

	fprintf(stderr, "Key not found in hashtable.\nFile: %s   Line: %d\n", file, line);
	errno = EINVAL;
	ht_errfunc = "_ht_get";
	return NULL;
}


void* _ht_get (HashTable* ht, key_type key, const char* file, int line) {
	ht_lock();
	void* result = ht_get_without_lock(ht, key, file, line);
	ht_unlock();
	return result;
}


void** _ht_all_get (HashTable* ht, size_t* out_count, const char* file, int line) {
	ht_lock();

	if (out_count == NULL) {
		fprintf(stderr, "Output count pointer is NULL.\nFile: %s   Line: %d\n", file, line);
		errno = EINVAL;
		ht_errfunc = "_ht_all_get";
		return NULL;
	}

	if (!ht_pre_execution_check(ht, file, line)) {
		ht_errfunc = "_ht_all_get";
		ht_unlock();
		return NULL;
	}

	void** values = calloc(ht->count, sizeof(void*));
	if (UNLIKELY(values == NULL)) {
		errno = ENOMEM;
		ht_errfunc = "_ht_all_get";
		ht_unlock();
		return NULL;
	}

	size_t idx = 0;
	for (size_t i = 0; i < ht->size; ++i) {
		Entry* entry = ht->buckets[i];
		while (entry) {
			values[idx++] = entry->value;
			entry = entry->next;
		}
	}

	ht_set_raw_without_lock(all_get_arr_entries, (key_type)values, values, file, line);

	*out_count = idx;  /* 正常なら ht->count と等しい */
	ht_unlock();
	return values;
}


bool _ht_all_release_arr (void* values, const char* file, int line) {
	return _ht_delete(all_get_arr_entries, (key_type)values, file, line);
}


static bool ht_delete_without_lock (HashTable* ht, key_type key, const char* file, int line) {
	if (!ht_pre_execution_check(ht, file, line)) {
		ht_errfunc = "_ht_delete";
		return false;
	}

	size_t index = hash_key(key, ht->size);
	Entry* prev = NULL;
	Entry* entry = ht->buckets[index];

	while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
		if (entry->key == key) {
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
	ht_errfunc = "_ht_delete";
	return false;
}


bool _ht_delete (HashTable* ht, key_type key, const char* file, int line) {
	ht_lock();
	bool result = ht_delete_without_lock(ht, key, file, line);
	ht_unlock();
	return result;
}


static void quit (void) {
	_ht_destroy(all_get_arr_entries, __FILE__, __LINE__);
	all_get_arr_entries = NULL;

	for (size_t i = 0; i < ht_entries->size; i++) {
		Entry* entry = ht_entries->buckets[i];
		while (entry != NULL) {  /* bucketsが確保時に初期化されていることが前提 */
			Entry* next = entry->next;
#ifdef DEBUG
			fprintf(stderr, "\nHashtable not destroyed!\nFile: %s   Line: %d\n", ((HtTrackEntry*)entry->value)->create_file, ((HtTrackEntry*)entry->value)->create_line);
#endif
			_ht_destroy(((HtTrackEntry*)entry->value)->ptr, __FILE__, __LINE__);
			entry = next;
		}
	}
	_ht_destroy(ht_entries, __FILE__, __LINE__);
	ht_entries = NULL;

	global_lock_quit();
}
