CC = gcc
FILES = fat.c 
OUT_EXE = fat

build: $(FILES)
	$(CC) -o $(OUT_EXE) $(FILES)

clean:
	rm -f *.o core
rebuild: clean build


