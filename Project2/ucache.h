//
// Created by syang on 2018/05/15.
//

#ifndef UCACHE_H
#define UCACHE_H

#define SET_NUM 4
#define ADDR_TAG(ADDR) (((unsigned int)ADDR)>>8)
#define ADDR_INDEX(ADDR) ((((unsigned int)ADDR)&0xF0)>>4)
#define ADDR_OFFSET(ADDR) (((unsigned int)ADDR)&0xF)

typedef struct cache_line {
    unsigned int data[4];
    unsigned int tag:24;
    unsigned int dirty:1;
    unsigned int valid:1;
    unsigned int ref_count;
    struct cache_line *next;
} cache_line_t;

typedef struct cache_set {
    cache_line_t *head;
    cache_line_t *tail;
    int n;
} cache_set_t;

typedef struct cache {
    cache_set_t sets[16];
    unsigned int enable;
    unsigned int access;
    unsigned int hit;
    unsigned int miss;
    unsigned int replace;
    unsigned int wb;
} cache_t;

typedef void(*cache_word_func)(cache_line_t *, word_t *, int);

void en_cache_set(cache_set_t *, cache_line_t *);

void de_cache_set(cache_set_t *);

int cache_access(cache_t *, md_addr_t, word_t *, cache_word_func);

int cache_read(cache_t *, md_addr_t, word_t *);

int cache_write(cache_t *, md_addr_t, word_t *);

void cache_word_read(cache_line_t *, word_t *, int);

void cache_word_write(cache_line_t *, word_t *, int);

cache_line_t *malloc_cache_line(md_addr_t);

void cache_write_back(cache_line_t *, int);

void cache_flush(cache_t *);

void add_into_cache_set(cache_set_t *, cache_line_t *, int);

#endif //UCACHE_H
