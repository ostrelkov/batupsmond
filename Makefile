all:
	$(CC) $(CFLAGS) -o batupsmond batupsmond.c

install:
	install batupsmond $(TARGET_DIR)/bin

clean:
	rm $(TARGET_DIR)/bin/batupsmond
