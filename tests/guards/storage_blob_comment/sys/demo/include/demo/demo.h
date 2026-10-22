#ifndef GUARD_STORAGE_BLOB_H
#define GUARD_STORAGE_BLOB_H

/* A storage block with a comment in it is still opaque: the comment is not a
 * field, and rejecting it would push authors towards an undocumented blob. */
#define GUARD_BLOB_STORAGE_SIZE 32u
#define GUARD_BLOB_STORAGE_ALIGN 8u

typedef struct guard_blob {
    /* Kept out of the public header on purpose (see #256). */
    _Alignas(GUARD_BLOB_STORAGE_ALIGN) unsigned char storage[GUARD_BLOB_STORAGE_SIZE];
} guard_blob_t;

#endif
