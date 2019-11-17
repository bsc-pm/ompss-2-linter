#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

int x = 5, y = 6;
int z[5][10];
int w[5];

int main(int argc, char *argv[]) {


// --------------------------------------------------------------
// E1: Missing access in task dataset
// --------------------------------------------------------------
#ifdef E1

	printf("\n");
	printf("(`WRN`) The task doesn't declare any read-write access to `x`.\n");
	#pragma oss task
	{
		x++;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The task doesn't declare any read access to `x`.\n");
	#pragma oss task out(x)
	{
		int y = x++;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The task declares a write access to `x` but also reads it.\n");
	printf("        (note: this is almost never an error, and it is mistakenly\n");
	printf("        considered as one only when using `-a 1`)\n");
	#pragma oss task out(x)
	{
		x = 5;
		int y = x;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The task doesn't declare any read access to `z[3:4][4:9]`,\n");
	printf("        nor any write access to `z[0:4][0:9]`.\n");
	#pragma oss task in(z[0;3][0;4])
	{
		for (int i = 0; i < 5; ++i)
			for (int j = 0; j < 10; ++j)
				z[i][j] += 2;
	}
	#pragma oss taskwait

#endif


// --------------------------------------------------------------
// E2: Missing access in task code
// --------------------------------------------------------------
#ifdef E2

	printf("\n");
	printf("(`NTC`) The task declares a read-write access to `x` but doesn't\n");
	printf("        actually access it.\n");
	#pragma oss task inout(x)
	{
		int y = 6;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`NTC`) The task declares a read-write access to `x` but only reads it.\n");
	#pragma oss task inout(x)
	{
		int y = x;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The task doesn't declare any write access to `w[4]`.\n");
	printf("        (note: this is almost always a correctness error,\n");
	printf("        which is why it is treated as a warning)");
	#pragma oss task out(w)
	{
		for (int i = 0; i < 4; ++i)
			w[i] = 0;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`NTC`) The task declares a read-write access to `z[0;5][0;10]`,\n");
	printf("        but only reads `z[0;3][0;4]`.\n");
	#pragma oss task inout(z[0;5][0;10])
	{
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 4; ++j)
				z[i][j] += 2;
	}
	#pragma oss taskwait

#endif


// --------------------------------------------------------------
// E3: Accessing a released region of a task dataset
// --------------------------------------------------------------
#ifdef E3

	printf("\n");
	printf("(`WRN`) The task read-writes `x` although its dependency has been released.\n");
	#pragma oss task inout(x)
	{
		x += 6;
		#pragma oss release inout(x)
		x += 7;
	}
	#pragma oss taskwait

#endif


// --------------------------------------------------------------
// E4: Releasing a non-existent region of a task dataset
// --------------------------------------------------------------
#ifdef E4

	printf("\n");
	printf("(`WRN`) The task is trying to release `x` although it has never\n");
	printf("        been declared as an access.\n");
	#pragma oss task inout(y)
	{
		y += 5;

		#pragma oss release out(x)
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The task is trying to release `w[2:5]` although it has never\n");
	printf("        been declared as an access.\n");
	#pragma oss task out(w[0:2])
	{
		for (int i = 0; i < 3; ++i)
			w[i] = i;

		#pragma oss release out(w[2:5])
	}
	#pragma oss taskwait

#endif


// --------------------------------------------------------------
// E5: Missing taskwait
// --------------------------------------------------------------
#ifdef E5

	printf("\n");
	printf("(`WRN`) The task reads `x` without waiting for its child task to terminate.\n");
	#pragma oss task inout(x)
	{
		int y;
		x = 5;

		#pragma oss task inout(x)
		x *= 6;

		y = x + 6;
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The child task may access `y` when it has been already invalidated\n");
	printf("        in the parent task code (stack memory).\n");
	#pragma oss task
	{
		{
			int y = 5;

			#pragma oss task inout(y)
			y *= 6;
		}
	}
	#pragma oss taskwait

	printf("\n");
	printf("(`WRN`) The child task may access `*y` when it has been already invalidated\n");
	printf("        in the parent task code (heap memory).\n");
	#pragma oss task
	{
		int *y = malloc(sizeof(int));

		#pragma oss task inout(*y)
		*y *= 6;

		free(y);
	}
	#pragma oss taskwait

#endif


// --------------------------------------------------------------
// E6: Missing region in the parent task dataset
// --------------------------------------------------------------
#ifdef E6

	printf("\n");
	printf("(`WRN`) The outermost task doesn't declare any weak read-write access to `x`.\n");
	#pragma oss task
	{
		#pragma oss task in(x)
		{
			int y = x + 1;
		}

		#pragma oss task out(x)
		{
			x = 12;
		}
	}
	#pragma oss taskwait

#endif


// --------------------------------------------------------------
// E7: Missing access in a child task dataset
// --------------------------------------------------------------
#ifdef E7

	printf("\n");
	printf("(`NTC`) A task declares a weak read-write access to `y` that is not declared\n");
	printf("        as a (weak) access by any of its child tasks.\n");
	#pragma oss task weakinout(x, y)
	{
		#pragma oss task inout(x)
		x *= 6;
	}
	#pragma oss taskwait

#endif


	return 0;
}
