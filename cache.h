#ifndef __CACHE__
#define __CACHE__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Cache is implemented in a linked list which has a header block */
typedef struct Cache{
	int obj_size;
	time_t last_access;
	char* p_url;
	struct Cache* p_next;
	char* response_body;
}Cache;

/* Function prototypes */
int Search_and_Transfer(char*, Cache*, int, void**);
void Write_and_Update(Cache*, int, char*, char*);
Cache* Find_LRU_Pre(Cache*);

#endif
