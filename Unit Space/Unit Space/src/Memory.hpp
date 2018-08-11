//
//  Memory.hpp
//  Unit Space
//
//  Created by Nigel Barber on 21/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <stdlib.h>

template<typename T>
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
    void alloc(size_t count) {
        if (count == 0)
            return;
        if (header)
            header->memory = ::realloc(header->memory, count * sizeof(T));
        else {
            header = (Header *)malloc(sizeof(Header));
            header->memory = malloc(count * sizeof(T));
            header->refCount = 1;
            header->end = 0;
        }
        header->size = count * sizeof(T);
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
    inline T *new_ptr() {
        if (header == nullptr)
            alloc(8);
        if (header->end * sizeof(T) == header->size)
            alloc(header->end * 1.5);
        return ((T *)header->memory + header->end++);
    }
    Memory& zero() {
        if (header)
            memset(header->memory, 0, header->size);
        return *this;
    }
    size_t count()                              { return header ? (header->end ? header->end : header->size / sizeof(T)) : 0; }
    size_t size()                               { return header ? header->size : 0; }
    T *get() const                              { return (T *) (header ? header->memory : nullptr); }
    inline T &operator[] (const size_t index)   { return ((T *)header->memory)[index]; }
    inline void push_back(T n)                  { *new_ptr() = n; }
    
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
