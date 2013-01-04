#include "cache.h"

static int read_cnt = 0; // initially 0
/* all initially 1 */
sem_t w_mutex; // writer lock
sem_t cnt_mutex; // lock for modifying read_cnt
sem_t mtime_mutex; // lock for modifying access-time

static int remain_size = MAX_CACHE_SIZE;  

int Rio_writen_my(int, void*, size_t); 

/* search Cache by url like: www.cmu.edu:80; if there's a cache hit, transfer its data back to client and update its access time; if a miss, return NULL */
int  Search_and_Transfer(char* provided_url, Cache* p_Cache, int fd, void ** ptr)
{
	*ptr = NULL;
	P(&cnt_mutex);
	read_cnt++;
	if(read_cnt == 1) 
		P(&w_mutex); //if it's the only reader reading the cache, forbidden writing
	V(&cnt_mutex);

	p_Cache = p_Cache->p_next; // skip linked list's header
	while(p_Cache != NULL)
	{
		if(!strcmp(provided_url, p_Cache->p_url)) // hit
		{
			if(Rio_writen_my(fd,p_Cache->response_body,p_Cache->obj_size) < 0){// if hit, transfer data to client from proxy directly
				P(&cnt_mutex);
				read_cnt--;
				if(read_cnt == 0) // no thread is reading the cache
					V(&w_mutex);
				V(&cnt_mutex);
				return -1;
			}

			fprintf(stderr,"hit\n");
			P(&mtime_mutex); // when one thread is updating the access time, forbidden another thread updating it.
			p_Cache->last_access = time(NULL);
			V(&mtime_mutex);

			P(&cnt_mutex);
			read_cnt--;
			if(read_cnt == 0) // no thread is reading the cache
				V(&w_mutex);
			V(&cnt_mutex);
			*ptr = p_Cache; //in this case, only have to ensure return value isn't NULL
			break;
		}
		p_Cache = p_Cache->p_next;
	}
	P(&cnt_mutex);
	read_cnt--;
	if(read_cnt == 0)
		V(&w_mutex);
	V(&cnt_mutex);
	return 0;
}


/* build cache block and its required data and update cache-list data structure: size/time/url/next/response_body */
void Write_and_Update(Cache* p_Cache, int read_size, char* url, char* response)
{
	P(&w_mutex); // lock the cache-list while writing cache;
	char* p_url = Malloc(strlen(url) + 1);
	char* p_response_body = Malloc(read_size);
	if(remain_size >= read_size) /* cache has enough memory, then construct a cache block and insert it in list */
	{
		/* Construct cache-block structure */
		Cache* p_new_block = Malloc(sizeof(struct Cache));
		p_new_block->obj_size = read_size;
		p_new_block->p_url = p_url;
		strcpy(p_new_block->p_url, url);
		p_new_block->response_body = p_response_body;
		memcpy(p_new_block->response_body, response, read_size);
		p_new_block->p_next = p_Cache->p_next;
		p_Cache->p_next = p_new_block; // insert new node after the header of list
		p_new_block->last_access = time(NULL);
		remain_size -= read_size; // modify cache's remain size
	}
	else // cache has not enough space, eviction happens
	{
		Cache *p_LRU_Pre, *p_temp;
		p_LRU_Pre = Find_LRU_Pre(p_Cache);
		while(p_LRU_Pre->p_next->obj_size + remain_size < read_size) /* If the LRU block's size cannot satisfy the space requirement of newly block, free its resource and itself, return space to cache */
		{
			p_temp = p_LRU_Pre->p_next;
			remain_size += p_temp->obj_size; // update cache size
			p_LRU_Pre->p_next = p_temp->p_next; // unlink LRU block and free the resource it points to & free cache block itself
			Free(p_temp->p_url);
			Free(p_temp->response_body);
			Free(p_temp);
			p_LRU_Pre = Find_LRU_Pre(p_Cache);
		}
		/* until find one LRU block, only have to free old resource and redirect pointers to newly-allocated area, and update access time */
		p_temp = p_LRU_Pre->p_next;
		remain_size = remain_size + p_temp->obj_size - read_size; // update cache size
		Free(p_temp->p_url);
		Free(p_temp->response_body);
		p_temp->p_url = p_url;
		strcpy(p_temp->p_url, url);
		p_temp->response_body = p_response_body;
		memcpy(p_temp->response_body, response, read_size);
		p_temp->obj_size = read_size;
		p_temp->last_access = time(NULL);
	}
	V(&w_mutex);
}


/* find the block just before the LRU block of current cache linkek list */
Cache* Find_LRU_Pre(Cache* p_Cache)
{
	time_t min_time = time(NULL);
	Cache* p_temp = p_Cache;
	while(p_Cache->p_next != NULL)
	{
		if(min_time > p_Cache->p_next->last_access)
		{
			p_temp = p_Cache;
			min_time = p_Cache->p_next->last_access;
		}
		p_Cache = p_Cache->p_next;
	}
	return p_temp;
}
/*
   void Destroy_Cache(Cache* p_Cache)
   {
   }*/
