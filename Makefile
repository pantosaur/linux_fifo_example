targets = main
objects = main.o

main : main.o
	cc -o fifo_example main.o

$(objects) : common.h

.PHONY : clean
clean:
	-rm -f $(targets) $(objects)
