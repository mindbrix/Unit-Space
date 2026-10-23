//
//  Memory.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <stdlib.h>
#import <stdint.h>
#import <string.h>
#import <unistd.h>
#import <new>
#import <algorithm>
#import <atomic>

// The VM page size: page-aligned memory is aligned to it, so Metal can use it as a buffer without copying it.
static inline size_t memoryPageSize() { static const size_t size = (size_t)getpagesize(); return size; }

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
    // Allocation failures throw std::bad_alloc, as std::vector's do, leaving the memory as it was. So do sizes beyond
    // size_t, which would otherwise wrap round to small allocations that writes then overrun.
    void alloc(size_t count) {
        if (count > SIZE_MAX / sizeof(T) || (page_aligned && count * sizeof(T) > SIZE_MAX - (memoryPageSize() - 1)))
            throw std::bad_alloc();
        size_t size = count * sizeof(T);
        void *memory = nullptr;
        if (page_aligned) {
            if (posix_memalign(& memory, memoryPageSize(), pageRounded(size)) != 0)
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
    inline T *new_ptr(size_t allocation) {
        if (header == nullptr)
            alloc(8);
        if (allocation > SIZE_MAX - header->end)
            throw std::bad_alloc();
        size_t end = header->end + allocation, count = header->size / sizeof(T);
        // Appends grow it by half, so their copying stays linear, but a request beyond that gets just what it needs:
        // memory written whole at its size is allocated once, at that size.
        if (end > count)
            alloc(std::max(size_t(count * 1.5), end));
        header->end = end;
        return ((T *)header->memory + end - allocation);
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
    inline static size_t pageRounded(size_t s)  { return (s + memoryPageSize() - 1) / memoryPageSize() * memoryPageSize(); }
private:
    void assign(const Memory& other) {
        if (this != & other) {
            this->~Memory();
            if ((header = other.header))
                header->refCount++;
        }
    }
    // Copies share a header; a Metal buffer made from the memory keeps one, which it releases on its own thread.
    struct Header { std::atomic<int> refCount; void *memory; size_t size, end; };
    Header *header = nullptr;
};
