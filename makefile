
tiny: tiny.o tinystd.o
	gcc -o tiny tiny.o tinystd.o

tinystd.o: tinystd.c
	gcc -c tinystd.c -std=c99
	
tiny.o: tiny.c
	gcc -c tiny.c -std=c99
	
