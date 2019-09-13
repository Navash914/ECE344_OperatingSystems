#include "common.h"
#include <stdbool.h>

bool is_str_int(char *str) {
	char *s = str;
	if (*s == '+' || *s == '-')
		s++;
	while (*s != '\0') {
		if (*s < '0' || *s > '9')
			return false;
		s++;
	}
	return true;
}

int fact(int n) {
	if (n == 1)
		return n;
	else
		return n * fact(n-1);
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		printf("Huh?\n");
	} else {
		if (!is_str_int(argv[1]))
			printf("Huh?\n");
		else {
			int n = atoi(argv[1]);
			if (n <= 0)
				printf("Huh?\n");
			else if (n > 12)
				printf("Overflow\n");
			else
				printf("%d\n", fact(n));
		}
	}
	return 0;
}
