EXECUTABLE=space
SOURCE=main.c
DATA=invaders.rom

.PHONY: run

$(EXECUTABLE): $(SOURCE)
	gcc $^ -o $@ -lpthread -lglut -lGL

run: $(EXECUTABLE)
	./$(EXECUTABLE) $(DATA)
