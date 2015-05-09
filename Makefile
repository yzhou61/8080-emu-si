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

.PHONY: run clean obj

$(TARGET): obj
	$(LD) $(TARGET) $(OBJECTS) $(LDFLAGS)

obj: $(SOURCES) $(INCLUDES)
	$(CC) $(CFLAGS) $(SOURCES)

run: $(TARGET)
	./$(TARGET) $(DATA)

clean:
	rm -f $(TARGET) $(OBJECTS)
