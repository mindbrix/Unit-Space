//
//  Memory.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <stdlib.h>
#import <string.h>
#import <new>
#import <algorithm>

constexpr size_t kMemoryPageSize = 4096;

template<typename T, bool page_aligned = false>
struct Memory {
    Memory()                            {}
    Memory(void *memory, size_t count)  { alloc(count); memcpy(header->memory, memory, header->size); }
    Memory(size_t count)                { alloc(count); }
    Memory(const Memory& other)         { assign(other); }
    Memory& operator= (Memory other)    { assign(other); return *this; }
    ~Memory() {
        if (header && (--(header->refCount) == 0))
            free(header->memory), free(header);
    }
    // Allocation failures throw std::bad_alloc, as std::vector's do, leaving the memory as it was.
    void alloc(size_t count) {
        size_t size = count * sizeof(T);
        void *memory = nullptr;
        if (page_aligned) {
            if (posix_memalign(& memory, kMemoryPageSize, pageRounded(size)) != 0)
                throw std::bad_alloc();
            if (header)
                memcpy(memory, header->memory, std::min(size, header->size)), free(header->memory);
        } else if (!(memory = header ? ::realloc(header->memory, size) : malloc(size)) && size)
            throw std::bad_alloc();
        if (!header) {
            if (!(header = (Header *)malloc(sizeof(Header)))) {
                free(memory);
                throw std::bad_alloc();
            }
            header->refCount = 1;
            header->end = 0;
        }
        header->memory = memory;
        header->size = size;
    }
    Memory clone() {
        if (header) {
            Memory c(header->size);
            memcpy(c.header->memory, header->memory, header->size);
            c.header->end = header->end;
            return c;
        }
        return Memory();
    }
    inline T *new_ptr(size_t allocation) {
        if (header == nullptr)
            alloc(8);
        header->end += allocation;
        size_t count = header->size / sizeof(T);
        if (header->end > count) {
            do {
                count *= 1.5;
            } while (count < header->end);
            alloc(count);
        }
        return ((T *)header->memory + header->end - allocation);
    }
    Memory& zero() {
        if (header)
            memset(header->memory, 0, header->size);
        return *this;
    }
    size_t count()                              { return header ? (header->end ?: header->size / sizeof(T)) : 0; }
    size_t size()                               { return header ? header->size : 0; }
    size_t pagedSize()                          { return header ? pageRounded(header->size) : 0; }
    T *get() const                              { return (T *) (header ? header->memory : nullptr); }
    inline T &operator[] (const size_t index)   { return ((T *)header->memory)[index]; }
    inline void push_back(T n)                  { *new_ptr(1) = n; }
    inline void empty()                         { if (header) header->end = 0; }
    inline static size_t pageRounded(size_t s)  { return (s + kMemoryPageSize - 1) / kMemoryPageSize * kMemoryPageSize; }
private:
    void assign(const Memory& other) {
        if (this != & other) {
            this->~Memory();
            if ((header = other.header))
                header->refCount++;
        }
    }
    struct Header { int refCount; void *memory; size_t size, end; };
    Header *header = nullptr;
};
