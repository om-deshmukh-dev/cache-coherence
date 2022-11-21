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

  // Implementation details
  // Calculate cache parameters Calculate parameters
  // 

  cache->n_cache_line = capacity / block_size;
  cache->n_set = capacity / (block_size * assoc);
  cache->n_offset_bit = log2(block_size);
  cache->n_index_bit = log2(cache->n_set);
  cache->n_tag_bit = 32 - (cache->n_index_bit + cache->n_offset_bit); 

  // Create the cache lines and the array of LRU bits
  // 
  // 
  // Implementation details

  cache->lines = malloc(cache->n_set * sizeof(cache_line_t*));
  for (int i = 0; i < cache->n_set; i++) {
    cache->lines[i] = malloc(assoc * sizeof(cache_line_t));
  }
  cache->lru_way = malloc(cache->n_set * sizeof(int));

  // Initialize cache tags to 0, dirty bits to false,
  // state to INVALID, and LRU bits to 0
  // Implementation details
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

/* Given a configured cache, the set index, and the way subject to a CPU action, changes the LRU index.
*  
* Example: a load/store on set 1 way 1 (lines[1][1])
* for a 4-way cache, if set 1 lru is way 1 (lru_way[1] = 1) -- change_lru(cache, 1, 1) sets lru_way[1] to 2
* for a 2-way cache -- change_lru(cache, 1, 1) sets lru_way[1] to 0
* if the lru way != the parameter way, change_lru() makes no change
*/
void change_lru(cache_t *cache, unsigned long index, int way){
  if (cache->lru_way[index] == way)
    cache->lru_way[index] = (cache->lru_way[index] + 1 >= cache->assoc) ? 0 : (cache->lru_way[index] + 1);
}

bool access_msi_cache(cache_t *cache, unsigned long addr, enum action_t action){
  unsigned long index = get_cache_index(cache, addr);
  unsigned long tag = get_cache_tag(cache, addr);
  log_set(index);
  cache_line_t* blockPtr = cache->lines[index]; //blockPtr points to the array of $ lines at index (the ways of the set)
  
  //check if addr is hit
  /*
  bool hit = false;
  int way;
  for (way = 0; way < cache->assoc; way++){
    if (blockPtr[way].tag == tag)
      hit = true;
      break;
  }
  enum state_t newState = blockPtr->state;
  bool newDirty = blockPtr->dirty_f;
  bool writeback = false;
  bool upgradeMiss = false;
  */

  for(int way = 0; way < cache->assoc; way++){   //check if addr is a hit
    if(blockPtr[way].tag == tag){
      blockPtr = &blockPtr[way];   //blockPtr now points to the block that was a hit
      log_way(way);
      if (blockPtr->state == INVALID){
        if (action == LOAD)
          blockPtr->state = SHARED;
        else if (action == STORE)
          blockPtr->state = MODIFIED;
        update_stats(cache->stats, true, false, false, action);
      }
      else if (blockPtr->state == SHARED){
        if (action == ST_MISS)
          blockPtr->state = INVALID;
        else if (action == STORE)
          blockPtr->state = MODIFIED;
        update_stats(cache->stats, true, false, (action == STORE), action); //upgrade miss = true only if action == store
      }
      else { //state == modified
        if (action == ST_MISS || action == LD_MISS){
          blockPtr->state = (action == ST_MISS) ? INVALID : SHARED;
          blockPtr->dirty_f = false;       //block will always be dirty if state == modified
          update_stats(cache->stats, true, true, false, action); //so we can writeback w/o checking dirty_f
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
  log_way(cache->lru_way[index]);
  if (action == LD_MISS || action == ST_MISS){
    update_stats(cache->stats, false, false, false, action);
    return false;
  }
  blockPtr = &blockPtr[cache->lru_way[index]]; //blockPtr now points to block about to get evicted
  update_stats(cache->stats, false, blockPtr->dirty_f, false, action);  //dirty bit matches writeback bool
  blockPtr->state = (action == LOAD) ? SHARED : MODIFIED;
  blockPtr->dirty_f = (action == STORE) ? true : false;
  blockPtr->tag = tag;
  change_lru(cache, index, cache->lru_way[index]);
  return false;
}

/*
  for (int i = 0; i < cache->assoc; i++){
    if (blockPtr[i].tag == tag){
      log_way(i);
      //newState = blockPtr[i].state;
      //newDirty = blockPtr[i].dirty_f;
      if (blockPtr[i].state == INVALID){
        if (action == LOAD){
          //change_lru(cache, index, i);
          //newState = SHARED;
          blockPtr[i].state = SHARED;
          update_stats(cache->stats, true, false, false, action);
        }
        else if (action == STORE){
          //newDirty = true;
          //newState = MODIFIED;
          blockPtr[i].dirty_f = true;
          blockPtr[i].state = MODIFIED;
          update_stats(cache->stats, true, false, false, action);
        }
        else{
          update_stats(cache->stats, true, false, false, action);
        }
      }
      else if (blockPtr[i].state == SHARED){
        if (action == ST_MISS){
          blockPtr[i].state = INVALID;
          update_stats(cache->stats, true, false, false, action);
        }
        else if (action == STORE){
          blockPtr[i].state = MODIFIED;
          blockPtr[i].dirty_f = true;
          update_stats(cache->stats, true, false, true, action);
        }
        else if (action == LOAD || action == LD_MISS)
          update_stats(cache->stats, true, false, false, action);
      }
      else if (blockPtr[i].state == MODIFIED){
        if (action == ST_MISS){
          if (blockPtr[i].dirty_f)
            update_stats(cache->stats, true, true, false, action);
          else
            update_stats(cache->stats, true, false, false, action); //will never happen, modified = dirty bit always true
          blockPtr[i].state = INVALID;
          blockPtr[i].dirty_f = false;
        }
        else if (action == LD_MISS){
          if (blockPtr[i].dirty_f)
            update_stats(cache->stats, true, true, false, action);
          else
            update_stats(cache->stats, true, false, false, action); // same here
          blockPtr[i].state = SHARED;
          blockPtr[i].dirty_f = false;
        }
        else if (action == LOAD || action == STORE)
          update_stats(cache->stats, true, false, false, action);
      }
      if (action == LOAD || action == STORE) {
        //change_lru(cache, index, i);
        if (cache->lru_way[index] == i){
          int lruIndex = i + 1;
          if (lruIndex >= cache->assoc)
            lruIndex = 0;
          cache->lru_way[index] = lruIndex;
        }
      }
      return true;
    }
  }
  cache_line_t* evicted = &blockPtr[cache->lru_way[index]];
  log_way(cache->lru_way[index]);
  if (action == LOAD && evicted->dirty_f == true) {
    evicted->dirty_f = false;
    evicted->state = SHARED;
    update_stats(cache->stats, false, true, false, action);
  }
  else if (action == STORE){
    if(evicted->dirty_f == true)
      update_stats(cache->stats, false, true, false, action);
    else
      update_stats(cache->stats, false, false, false, action);
    evicted->dirty_f = true;
    evicted->state = MODIFIED;
  }
  else {
    update_stats(cache->stats, false, false, false, action);
    if (action == LD_MISS || action == ST_MISS)
      return false;
    evicted->state = SHARED;
  }
  evicted->tag = tag;
  if (cache->lru_way[index] + 1 >= cache->assoc) //write lru wraparound helper function?   send cache and index as parameters, function sets array w/ new lru way, no return
    cache->lru_way[index] = 0;
  else
    cache->lru_way[index] += 1;
  return false;
}
*/


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
      
        /*
        if (blockPtr[i].dirty_f)
          update_stats(cache->stats, true, true, false, action);
        else
          update_stats(cache->stats, true, false, false, action);
        return true;
      }
      */
      /*
      if (cache->lru_way[index] == i){
        int lruIndex = i + 1;
        if (lruIndex >= cache->assoc)
          lruIndex = 0;
        cache->lru_way[index] = lruIndex;
      }
      update_stats(cache->stats, true, false, false, action);
      return true;
    }
    */

  //for cache misses:
  blockPtr = &blockPtr[cache->lru_way[index]];  //blockPtr now points to block about to be evicted
  log_way(cache->lru_way[index]);
  //WARNING dirty should be false if invalid, but might need LD/ST actions writeback ONLY (dont want LD/ST_MISS to writeback)
  update_stats(cache->stats, false, blockPtr->dirty_f, false, action); //simulates writeback if evicted block is dirty
  if (action == LOAD || action == STORE){
    blockPtr->dirty_f = (action == STORE); //dirty bit = true if action == store
    blockPtr->tag = tag;
    blockPtr->state = VALID;
    change_lru(cache, index, cache->lru_way[index]);
  }
  return false;
}

/*
  if (action == LOAD && blockPtr->dirty_f == true) {
    blockPtr->dirty_f = false;
    update_stats(cache->stats, false, true, false, action);
  }
  else if (action == STORE){
    if(blockPtr->dirty_f == true)
      update_stats(cache->stats, false, true, false, action);
    else
      update_stats(cache->stats, false, false, false, action);
    blockPtr->dirty_f = true;
  }
  else
    update_stats(cache->stats, false, false, false, action);
    if (action == LD_MISS || action == ST_MISS)
      return false;
  blockPtr->tag = tag;
  blockPtr->state = VALID;
  if (cache->lru_way[index] + 1 >= cache->assoc) //write lru wraparound helper function?   send cache and index as parameters, function sets array w/ new lru way, no return
    cache->lru_way[index] = 0;
  else
    cache->lru_way[index] += 1;
  return false;
}
*/