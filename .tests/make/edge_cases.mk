# Makefile Edge Cases
# Simple comment
all:
	@echo "command" # comment
	# comment in command
	gcc -o test test.c \
	    -lm # line continuation comment

# Multi-line
define FOO
  line1
  line2
endef
