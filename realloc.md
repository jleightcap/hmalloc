# realloc
- pointer NULL
    malloc given size

- size equal to zero
    free
    return null pointer

- size equal to the given size:
    do nothing
    (return given pointer)

- given size less than size
    return the difference to the freelist
    return old pointer

- given size greater than current size
    malloc new size,
    memcpy contents from old to new pointer
    free old pointer
    return new pointer


