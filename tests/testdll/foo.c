#include <stdio.h>

typedef struct _complex_rec {
    double r;
    double i;
} complex, *complex_ptr;

typedef struct _simple_rec {
    int integer_field;
    short short_field;
    long long_field;
    complex complex_field;
    double double_field;
    char char_field;
    int scan_sum;
} simple_rec, *simple_rec_ptr;


int 
filter(void* input, void* output, void *data)
{

	return ((simple_rec_ptr)input)->long_field %2;
}

