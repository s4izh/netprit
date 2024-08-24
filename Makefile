BUILD ?= build

netprit:
	@mkdir -p $(BUILD)
	gcc -o $(BUILD)/main src/main.c -lpthread

clean:
	rm -rf $(BUILD)

test0:
	ncat localhost 5000 < /dev/null
test1:
	ncat localhost 5001 < /dev/null

.PHONY: clean
