C_FILES     := $(wildcard *.c)
OBJ_FILES   := $(addprefix ./,$(notdir $(C_FILES:.c=.o)))
C_FLAGS     := -std=c11 -Wextra -pedantic -Wall
C_FLAGS		+= -g
PROG_NAME   := false_sharing_valgrind
LD_LIBS     := -lpthread

$(PROG_NAME): $(OBJ_FILES)
	gcc -o $@ $^ $(LD_LIBS)

%.o: %.c
	gcc $(C_FLAGS) -c -o $@ $<

clean:
	rm -rf *.o *.d $(PROG_NAME)

C_FLAGS		+= -MMD
-include $(OBJ_FILES:.o=.d)

