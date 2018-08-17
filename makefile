http_server2:http_server2.c
	gcc $^ -o $@ -lpthread

.PHONY:clean
	rm http_server1
