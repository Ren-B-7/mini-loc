# Makefile Edge Cases
# Simple Complexity item
# This should be 6 and if counting comments 7
# 5 code lines, 1 blank line, 8 comment lines
all:
	@if [ statement ];
		#Comment line
	@if [ a || b ];
		# Comment line
	for( a && b);
		# Comment
	a && b
	# if [ statement in a comment];

