logconv: logconv.c
	gcc -o logconv -lz -s -O3 logconv.c

clean:
	rm -f logconv
