int f();
#define F()	f()

void foo()
{
#pragma scop
	F();
#pragma endscop
}
