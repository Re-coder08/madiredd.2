all: clean
	gcc -Wall -g oss.c -o oss -lrt
	gcc -Wall -g user.c -o user -lrt

clean:
	rm -rf oss user *.log
