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

/* 
*  Given a configured cache, the set index, and the way subject to a CPU action, changes the LRU index.
*/
void change_lru(cache_t *cache, unsigned long index, int way){
    cache->lru_way[index] = (way + 1 >= cache->assoc) ? 0 : (way + 1);
}

/*
* This method has the same functionallity as
* access_cache() below, but specifically for msi protocol caches
*/
bool access_msi_cache(cache_t *cache, unsigned long addr, enum action_t action){
  unsigned long index = 0;
  if (cache->n_index_bit != 0)  //only get cache index if cache != fully associative
    index = get_cache_index(cache, addr);
  unsigned long tag = get_cache_tag(cache, addr);
  log_set(index);
  cache_line_t* blockPtr = cache->lines[index]; //blockPtr points to the array of $ lines at index (the ways of the set)

  int way_hitInvalid = 0;
  for(int way = 0; way < cache->assoc; way++){   //check if addr is a hit
    if(blockPtr[way].tag == tag){
      blockPtr = &blockPtr[way];   //blockPtr now points to the block that was a hit
      if(blockPtr->state == INVALID){ //if state invalid, miss. go though miss prot below
        way_hitInvalid = way;
        break;
      }
      log_way(way);

      if (blockPtr->state == SHARED){
        if (action == ST_MISS) {
          blockPtr->state = INVALID;
          update_stats(cache->stats, true, false, false, action);
        }
        else if (action == STORE) {
          blockPtr->state = MODIFIED;
          blockPtr->dirty_f = true;
          update_stats(cache->stats, false, false, true, action);
          change_lru(cache, index, way);
          return false;
        }
        else
          update_stats(cache->stats, true, false, false, action);
      }
      else { //state == modified
        if (action == ST_MISS || action == LD_MISS){
          blockPtr->state = (action == ST_MISS) ? INVALID : SHARED;
          update_stats(cache->stats, true, (blockPtr->dirty_f), false, action);
          blockPtr->dirty_f = false;
        }
        else
          update_stats(cache->stats, true, false, false, action);
      }
      if (action == STORE || action == LOAD){
        change_lru(cache, index, way);    //change lru for every CPU action
        if (action == STORE)    //block is dirty after every store, no matter what state
          blockPtr->dirty_f = true;  
      }
      return true;
    }
  }

  //if addr didn't hit
  if (action == LD_MISS || action == ST_MISS){
    log_way(cache->lru_way[index]);
    update_stats(cache->stats, false, false, false, action);
    return false;
  }

  if (blockPtr->tag == tag){  //miss because state was invalid
    log_way(way_hitInvalid);
    update_stats(cache->stats, false, false, false, action);
    blockPtr->state = (action == LOAD) ? SHARED : MODIFIED;
    blockPtr->dirty_f = (action == STORE) ? true : false;
    change_lru(cache, index, way_hitInvalid);
  }
  else {  //miss because no tag match
    log_way(cache->lru_way[index]);
    blockPtr = &blockPtr[cache->lru_way[index]]; //blockPtr now points to block about to get evicted
    update_stats(cache->stats, false, blockPtr->dirty_f, false, action);  //dirty bit matches writeback bool
    blockPtr->state = (action == LOAD) ? SHARED : MODIFIED;
    blockPtr->dirty_f = (action == STORE) ? true : false;
    blockPtr->tag = tag;
    change_lru(cache, index, cache->lru_way[index]);
  }
  return false;
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
  if (cache->protocol == MSI)  //if cache implements MSI protocol, use access_msi_cache functon
    return access_msi_cache(cache, addr, action);
  unsigned long index = 0;
  if (cache->n_index_bit != 0)
    index = get_cache_index(cache, addr);
  log_set(index);
  cache_line_t* blockPtr = cache->lines[index];
  unsigned long tag = get_cache_tag(cache, addr);
  for (int i = 0; i < cache->assoc; i++){
    if (blockPtr[i].tag == tag && blockPtr[i].state == VALID){
      log_way(i);
      if (action == LD_MISS || action == ST_MISS){
        if (cache->protocol == NONE){
          update_stats(cache->stats, true, false, false, action);
          return true;
        }
        blockPtr[i].state = INVALID;
        update_stats(cache->stats, true, blockPtr[i].dirty_f, false, action);
        blockPtr[i].dirty_f = false;
        return true;
      }
      if (action == STORE)   //set dirty bit if store, dont change if not
        blockPtr[i].dirty_f = true;
      change_lru(cache, index, i);
      update_stats(cache->stats, true, false, false, action);
      return true;
    }
  }
  //for cache misses:
  blockPtr = &blockPtr[cache->lru_way[index]];  //blockPtr now points to block about to be evicted
  log_way(cache->lru_way[index]);
  if (cache->protocol == NONE && (action == LD_MISS || action == ST_MISS)){  //with no protocol, ld/st_misses have no effect
    update_stats(cache->stats, false, false, false, action);
    return false;
  }
  update_stats(cache->stats, false, blockPtr->dirty_f, false, action); //simulates writeback if evicted block is dirty
  if (action == LOAD || action == STORE){
    blockPtr->dirty_f = (action == STORE); //dirty bit = true if action == store
    blockPtr->tag = tag;
    blockPtr->state = VALID;
    change_lru(cache, index, cache->lru_way[index]);
  }
  return false;
}