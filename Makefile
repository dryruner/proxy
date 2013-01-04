CC = gcc
CFLAGS = -g -Wall# -DDEBUG
LDFLAGS = -lpthread

all:
	gcc -g -Wall proxy.c cache.c csapp.c -lpthread -o proxy

#cache.o: cache.c cache.h
#	$(CC) $(CFLAGS) -c cache.c

#csapp.o: csapp.c csapp.h
#	$(CC) $(CFLAGS) -c csapp.c

#proxy.o: proxy.c cache.c
#	$(CC) $(CFLAGS) -c proxy.c cache.c

#proxy: proxy.o csapp.o

submit:
	(make clean; cd ..; tar czvf proxylab.tar.gz proxylab-handout)

clean:
	rm -f *~ *.o proxy core

