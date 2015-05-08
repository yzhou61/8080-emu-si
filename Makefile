EXECUTABLE=space
SOURCE=main.c
DATA=invaders.rom

.PHONY: run clean

$(EXECUTABLE): $(SOURCE)
	gcc $^ -o $@ -lpthread -lglut -lGL

run: $(EXECUTABLE)
	./$(EXECUTABLE) $(DATA)

clean:
	rm $(EXECUTABLE)
