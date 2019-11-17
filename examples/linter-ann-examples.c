#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int x = 5;
int w[5];

int main(int argc, char *argv[]) {


	printf("\n");
	printf("Accesses within `oss lint` pragma regions are ignored.\n");
	printf("E2 errors will be shown.\n");
	#pragma oss task in(x)
	{
		#pragma oss lint
		{
			int y = x;
		}
	}
	#pragma oss taskwait

	#pragma oss task out(x)
	{
		#pragma oss lint
		{
			x = 5;
		}
	}
	#pragma oss taskwait

	#pragma oss task inout(x)
	{
		#pragma oss lint
		{
			x += 5;
		}
	}
	#pragma oss taskwait

	printf("\n");
	printf("The previous errors can be fixed by declaring accesses\n");
	printf("in the `oss lint` dataset that match with the task dataset.\n");
	printf("(note: the accesses in the pragma regions are ignored,\n");
	printf("so even if they don't match with the `oss lint` dataset,\n");
	printf("the tool won't detect this)\n");
	printf("No errors will be shown.\n");
	#pragma oss task in(x)
	{
		#pragma oss lint in(x)
		{
			x = 5;
		}
	}
	#pragma oss taskwait

	#pragma oss task out(x)
	{
		#pragma oss lint out(x)
		{
			int y = x;
		}
	}
	#pragma oss taskwait

	#pragma oss task inout(x)
	{
		#pragma oss lint inout(x)
		{
		}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Nested pragma regions are ignored, together with their accesses,\n");
	printf("for composability purposes.\n");
	printf("No errors will be shown.\n");
	#pragma oss task
	{
		#pragma oss lint
		{
			#pragma oss lint in(x)
			{
			}

			#pragma oss lint out(x)
			{
			}

			#pragma oss lint inout(x)
			{
			}
			#pragma oss taskwait
		}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Verified tasks are ignored.\n");
	printf("No errors will be shown.\n");
	#pragma oss task in(x) verified(1)
	{
	}
	#pragma oss taskwait

	#pragma oss task out(x) verified(1)
	{
	}
	#pragma oss taskwait

	#pragma oss task inout(x) verified(1)
	{
	}
	#pragma oss taskwait


	printf("\n");
	printf("Verified tasks are ignored (conditional expression).\n");
	printf("No errors will be shown.\n");
	#pragma oss task weakout(w)
	{
		for (int i = 0; i < 5; ++i)
			#pragma oss task out(w[i]) verified(i % 2)
			{
				if (i % 2 == 0) {
					w[i] = 7;
				}
			}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Verified tasks are ignored (conditional expression).\n");
	printf("E1 errors will be shown.\n");
	#pragma oss task weakout(w)
	{
		for (int i = 0; i < 5; ++i)
			#pragma oss task out(w[i]) verified(i % 2 == 0)
			{
				w[i] += 7;
			}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Tasks inside ignored regions are ignored.\n");
	printf("No errors will be shown.\n");
	#pragma oss task
	{
		#pragma oss lint
		{
			#pragma oss task in(x)
			{
				x = 5;
			}

			#pragma oss task out(x)
			{
				int y = x;
			}

			#pragma oss task inout(x)
			{
			}
		}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Verified tasks inside ignored regions are ignored,\n");
	printf("for composability purposes.\n");
	printf("No errors will be shown.\n");
	#pragma oss task
	{
		#pragma oss lint
		{
			#pragma oss task in(x) verified(1)
			{
				x = 5;
			}

			#pragma oss task out(x) verified(1)
			{
				int y = x;
			}

			#pragma oss task inout(x) verified(1)
			{
			}
		}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Tasks inside verified tasks are ignored.\n");
	printf("No errors will be shown.\n");
	#pragma oss task verified(1)
	{
		#pragma oss task in(x)
		{
			x = 5;
		}

		#pragma oss task out(x)
		{
			int y = x;
		}

		#pragma oss task inout(x)
		{
		}
	}
	#pragma oss taskwait


	printf("\n");
	printf("Verified tasks inside verified tasks are ignored,\n");
	printf("for composability purposes.\n");
	printf("No errors will be shown.\n");
	#pragma oss task verified(1)
	{
		#pragma oss lint
		{
			#pragma oss task in(x) verified(1)
			{
				x = 5;
			}

			#pragma oss task out(x) verified(1)
			{
				int y = x;
			}

			#pragma oss task inout(x) verified(1)
			{
			}
		}
	}
	#pragma oss taskwait


	return 0;
}
