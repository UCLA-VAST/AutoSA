void foo()
{
int i;
int a;

for (i = 0; i < 10; ++i) {
#pragma scop
	a = 5;
#pragma endscop
}

}
