prog1: prog1/text_processing
	./prog1/text_processing

prog2: prog2/sort_array
	./prog2/sort_array prog2/dataSet2/datSeq16M.bin

debug: prog1/text_processing.c prog2/sort_array.c prog2/sort_control.c
	gcc -g -o prog1/text_processing prog1/text_processing.c -lpthread
	gcc -g -o prog2/sort_array prog2/sort_array.c prog2/sort_control.c -lpthread -lm

prog2/sort_array: prog2/sort_array.c prog2/sort_control.c
	gcc -Wall -O3 -o prog2/sort_array prog2/sort_array.c prog2/sort_control.c -lpthread -lm

prog1/text_processing: prog1/text_processing.c
	gcc -Wall -O3 -o prog1/text_processing prog1/text_processing.c -lpthread