int f(void);
int P(void);

void foo(int n)
{
	int s;

	while (P()) {
		s = f();
loop:		goto loop;
	}
}
