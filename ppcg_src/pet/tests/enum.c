enum type {
	type_a = 0,
	type_b,
	type_c,
	type_last
};

void foo()
{
	int a[type_last];

#pragma scop
	a[type_b] = 5;
#pragma endscop
}
