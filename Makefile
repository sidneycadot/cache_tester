
.PHONY : clean

CFLAGS = -W -Wall -O3 -pthread

cache_tester : cache_tester.c

clean :
	$(RM) cache_tester *~
