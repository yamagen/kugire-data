CC = cc
CFLAGS = -O2 -std=c11 -Wall -Wextra
TARGET = kugire
SRC = kugire.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET) 01kokin.txt kokin-pos.txt > kokin-kugire.json

.PHONY: all clean run
