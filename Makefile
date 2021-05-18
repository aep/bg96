COBJ=src/main.o src/io.o src/mux.o

main: $(COBJ) src/io.h src/mux.h
	$(CC) -o $@ $(COBJ)

clean:
	rm -f $(COBJ)
