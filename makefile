http_server1:http_server1.c
	gcc $^ -o $@ -lpthread

.PHONY:clean
	rm http_server1
