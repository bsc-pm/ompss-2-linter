CC:=mcc
CFLAGS:=-O0 --ompss-2 -g

SRC_1:=linter-main-examples.c
SRC_2:=linter-ann-examples.c
PROGS:=E1 E2 E3 E4 E5 E6 E7 ann

all: $(PROGS)

E1: $(SRC_1)
	$(CC) -DE1 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE1 $(CFLAGS) --line-markers $? -o $@

E2: $(SRC_1)
	$(CC) -DE2 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE2 $(CFLAGS) --line-markers $? -o $@

E3: $(SRC_1)
	$(CC) -DE3 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE3 $(CFLAGS) --line-markers $? -o $@

E4: $(SRC_1)
	$(CC) -DE4 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE4 $(CFLAGS) --line-markers $? -o $@

E5: $(SRC_1)
	$(CC) -DE5 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE5 $(CFLAGS) --line-markers $? -o $@

E6: $(SRC_1)
	$(CC) -DE6 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE6 $(CFLAGS) --line-markers $? -o $@

E7: $(SRC_1)
	$(CC) -DE7 $(CFLAGS) -y $? -o $@_post.c
	$(CC) -DE7 $(CFLAGS) --line-markers $? -o $@

ann: $(SRC_2)
	$(CC) $(CFLAGS) -y $? -o $@_post.c
	$(CC) $(CFLAGS) --line-markers $? -o $@

clean:
	rm -rf $(PROGS) $(PROGS:=_post.c)
