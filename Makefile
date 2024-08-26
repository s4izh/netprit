BUILD ?= build
CC	  ?= gcc

netprit:
	@mkdir -p $(BUILD)
	$(CC) -o $(BUILD)/main src/main.c -lpthread

clean:
	rm -rf $(BUILD)

test0:
	ncat localhost 5000 < /dev/null
test1:
	ncat localhost 5001 < tests/shell-script.sh

.PHONY: clean
