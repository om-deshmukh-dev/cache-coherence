#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "cache.h"
#include "print_helpers.h"

cache_t *make_cache(int capacity, int block_size, int assoc, enum protocol_t protocol, bool lru_on_invalidate_f){
  cache_t *cache = malloc(sizeof(cache_t));
  cache->stats = make_cache_stats();
  
  cache->capacity = capacity;      // in Bytes
  cache->block_size = block_size;  // in Bytes
  cache->assoc = assoc;            // 1, 2, 3... etc.

  // Calculate cache parameters
  cache->n_cache_line = capacity / block_size;
  cache->n_set = capacity / (block_size * assoc);
  cache->n_offset_bit = log2(block_size);
  cache->n_index_bit = log2(cache->n_set);
  cache->n_tag_bit = 32 - (cache->n_index_bit + cache->n_offset_bit);

  // Create the cache lines and the array of LRU bits
  cache->lines = malloc(cache->n_set * sizeof(cache_line_t*));
  for (int i = 0; i < cache->n_set; i++) {
    cache->lines[i] = malloc(assoc * sizeof(cache_line_t));
  }
  cache->lru_way = malloc(cache->n_set * sizeof(int));

  // Initialize cache tags to 0, dirty bits to false, state to INVALID, and LRU bits to 0
  for (int i = 0; i < cache->n_set; i++) {
    for (int j = 0; j < cache->assoc; j++) {
      cache->lines[i][j].tag = 0;
      cache->lines[i][j].dirty_f = false;
      cache->lines[i][j].state = INVALID;
    }
  }
  for(int i = 0; i < cache->n_set; i++){
    cache->lru_way[i] = 0;
  }
  
  cache->protocol = protocol;
  cache->lru_on_invalidate_f = lru_on_invalidate_f;
  
  return cache;
}

/* Given a configured cache, returns the tag portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_tag(0b111101010001) returns 0b1111
 * in decimal -- get_cache_tag(3921) returns 15 
 */
unsigned long get_cache_tag(cache_t *cache, unsigned long addr) {
  return addr >> (32 - cache->n_tag_bit);
}

/* Given a configured cache, returns the index portion of the given address.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_index(0b111101010001) returns 0b0101
 * in decimal -- get_cache_index(3921) returns 5
 */
unsigned long get_cache_index(cache_t *cache, unsigned long addr) {
  unsigned long index = addr << (cache->n_tag_bit + 32);
  index = index >> (cache->n_tag_bit + cache->n_offset_bit + 32);
  return index;
}

/* Given a configured cache, returns the given address with the offset bits zeroed out.
 *
 * Example: a cache with 4 bits each in tag, index, offset
 * in binary -- get_cache_block_addr(0b111101010001) returns 0b111101010000
 * in decimal -- get_cache_block_addr(3921) returns 3920
 */
unsigned long get_cache_block_addr(cache_t *cache, unsigned long addr) {
  addr = addr >> cache->n_offset_bit;
  return addr << cache->n_offset_bit;
}


/* this method takes a cache, an address, and an action
 * it proceses the cache access. functionality in no particular order: 
 *   - look up the address in the cache, determine if hit or miss
 *   - update the LRU_way, cacheTags, state, dirty flags if necessary
 *   - update the cache statistics (call update_stats)
 * return true if there was a hit, false if there was a miss
 * Use the "get" helper functions above. They make your life easier.
 */
bool access_cache(cache_t *cache, unsigned long addr, enum action_t action) {
  if(cache->assoc == 1){
    cache_line_t* blockPtr = cache->lines[get_cache_index(cache, addr)];
    unsigned long tag = get_cache_tag(cache, addr);
    if (blockPtr->tag != tag || blockPtr->state == INVALID) {
      blockPtr->tag = tag;
      blockPtr->state = VALID;
      update_stats(cache->stats, false, false, false, action);
      return false;
    }
  update_stats(cache->stats, true, false, false, action);
  }
  return true;  // cache hit should return true
}
