targets = fifo_example
objects = main.o

fifo_example : main.o
	cc -o fifo_example main.o

$(objects) : common.h

.PHONY : clean
clean:
	-rm -f $(targets) $(objects)
