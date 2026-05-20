BEGIN {
    # AWK test
    print "Start"
}

{
    print $0
}

END {
    print "End"
}
