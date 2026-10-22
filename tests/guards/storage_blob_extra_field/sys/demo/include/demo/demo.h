#ifndef GUARD_STORAGE_BLOB_BAD_H
#define GUARD_STORAGE_BLOB_BAD_H

/* The blob plus one reachable field: the caller can read and write module state,
 * so the size contract is decoration. The gate must reject this. */
#define GUARD_BLOB_BAD_STORAGE_SIZE 32u
#define GUARD_BLOB_BAD_STORAGE_ALIGN 8u

typedef struct guard_blob_bad {
    _Alignas(GUARD_BLOB_BAD_STORAGE_ALIGN) unsigned char storage[GUARD_BLOB_BAD_STORAGE_SIZE];
    int pending_head;
} guard_blob_bad_t;

#endif
