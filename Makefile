TARGET=space
DATA=invaders.rom
SOURCE+=main.c
SOURCE+=8080e.c

CC     = gcc -c
CFLAGS = -Wall -I. -g
LD     = gcc -o
LDFLAGS = -Wall -lpthread -lglut -lGL -g
RM     = rm -f

SOURCES  := $(wildcard *.c)
INCLUDES := $(wildcard *.h)
OBJECTS  := $(SOURCES:.c=*.o)

.PHONY: run clean

$(TARGET): $(OBJECTS)
	$(LD) $(TARGET) $(OBJECTS) $(LDFLAGS)

%.o: %.c $(INCLUDES)
	$(CC) $(CFLAGS) $<

run: $(TARGET)
	./$(TARGET) $(DATA) 2

clean:
	rm -f $(TARGET) $(OBJECTS)
