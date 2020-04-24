# linux_fifo_example
program that communicates using named pipes with another instance

`usage: ./fifo_example fifo_i fifo_o`

Each instance manages it's read fifo, and expects the write fifo to be created by another process
To test the program, launch 2 instances. For example:

`./fifo_example f1 f2`

`./fifo_example f2 f1`

