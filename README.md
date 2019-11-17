A run-time tool to detect parallelization errors in OmpSs-2 applications.

# Description
Programming models for task-based parallelization based on compile-time directives are very effective at uncovering the parallelism available in HPC applications. However, the process of correctly annotating complex applications is error-prone and still hinders the general adoption of these models. OmpSs-2 Linter is a debugging/linting tool which takes as input an application parallelized using the OmpSs-2 programming model and provides a report of all the potential parallelization errors that were encountered by tracing the application. Programmers can then use the tool to make sure that nothing was left behind during the annotation process.

Technically, OmpSs-2 Linter is a run-time dynamic binary instrumentation tool built on top of Intel PIN. When an OmpSs-2 application is run through this tool, memory accesses issued by tasks are recorded and temporarily saved to an in-memory storage area. When a task completes, the recorded memory accesses issued by that task are processed and compared with task-level information (e.g., dependencies) to check for potential parallelization errors. For each of such errors a report message is generated to inform the user about the problem. The message comes with additional contextual information such as: address and size of the mismatching memory access (if any) along with its access mode (i.e., read or write); name of the involved dependency (if any) with the expected directionality (e.g., `in`, `out`, `inout`); variable name (if found); task invocation point in the source code (i.e., line in the respective file).

# Requirements

Currently OmpSs-2 Linter only runs on GNU/Linux systems. It also requires the following software and versions:

| Software | Version |
|---|---|
| OmpSs-2 | >= [2019.11](https://github.com/bsc-pm/ompss-2-releases/releases/tag/2019.11) |
| Intel PIN | [3.11](https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.11-97998-g7ecce2dac-gcc-linux.tar.gz) |
| Libelf | 0.8.13 |

NOTE: It is necessary to compile Nanos6 (in the OmpSs-2 package) with the `--enable-lint-instrumentation` and the `--with-symbol-resolution=ifunc` configure flags.

# Download

Download the latest OmpSs-2 Linter release at [this link](https://github.com/bsc-pm/ompss-2-linter/archive/master.zip).

# Installation

In order to install OmpSs-2 Linter, we need to inform the install script about the location of the Intel PIN binary:

	export PIN_ROOT=<path to PIN root directory>

Then,the install script can be invoked as follows:

	./install.sh

# Usage

In order to execute OmpSs-2 Linter, we need to inform it about the location of the Intel PIN binary:

	export PIN_ROOT=<path to PIN root directory>

Then, OmpSs-2 Linter can be executed via the launcher:

	./ompss-2-linter.sh <executable>

Where `<executable>` is the path to the OmpSs-2 application that needs to be verified.

NOTE: In order to fully exploit the capabilities of the tool, the OmpSs-2 application must be compiled with the following flags:

* (C/C++ compiler) `-O0` to disable compiler optimizations
* (C/C++ compiler)`-g` to emit debug information
* (Mercurium) `--line-markers` to map lines in the original source program to lines in the transformed source program

## Parameters

The launcher accepts some additional parameters:

* `-d <0..6>` (default: `0`)
	- The greater the output level, the more verbose is the tool. Notice that an output level of `0` still shows all the encountered errors in the application.
* `-r <NTC|WRN>` (default: `WRN`)
	- Specifies the minimum level of error which are printed by the tool. `NTC` means that both notices and warnings are printed. `WRN` means that only warnings are printed. See "Errors" for an explanation of the two classes of errors.
* `-e`
   - Enable the generation of an extended report, showing an error for each instance of a task rather than for each task in source code. This option typically increases the report size by orders of magnitude and is useful to debug an application when the initial report is not sufficiently detailed to understand the cause of the error.
* `-a <0..1>` (default: `1`)
	- `0` means that each access that was originally detected during execution is treated independently. This option generates the least amount of false positives at the expense of the highest execution overhead.
	- `1` means that all accesses that come from the same instruction and that target contiguous addresses in memory are merged into a single access. This option can speed-up execution in case of multi-dimensional array traversals, but sometimes generates false positives. See "Errors" for an explanation of the false positives detected while aggregating accesses.
* `-p <seconds>` (default: `0`)
	- To run the tool in debug mode. The debug mode pauses the execution of the tool during the first `<seconds>` seconds to allow to attach a separate GDB session to it. This is only useful to debug errors in the tool itself, rather than errors in the application.
* `-o <outputs_directory>` (default: `linter-data`)
	- Path to the directory used by the tool to save output data (not meant to be read by the user). Useful when running multi-process programs, or MPI programs on a shared distributed filesystem, to make sure that each process writes on its own directory.

For example, to verify an application while generating the minimum useful amount of information in the report, we can launch the tool as follows:

	./ompss-2-linter.sh -d 0 -a 1 -r NTC <executable>

To generate the maximum useful amount of information, we can launch the tool with the following parameters:

	./ompss-2-linter.sh -d 6 -a 0 -r WRN -e <executable>

When running the tool with MPI applications, and assuming a system with a batch scheduler such as SLURM, the tool can be launched through the following `sbatch` script:

	#!/bin/bash
	
	# SBATCH options...
	
	srun ./ompss-2-linter.sh -o linter-data_$SLURM_NODEID-$SLURM_PROCID

# Errors

To date, the tool is able to detect the following potential errors, grouped into **warnings** (`WRN`), which may affect the _correctness_ of the parallelization, and **notices** (`NTC`), which may affect the _performance_ of the parallelization. Note that these errors are only _potential_, meaning that they may be due to either bugs or explicit design choices by the programmer.

Some examples for each class and type of errors are provided in the `examples/linter-main-examples.c` directory.

| Class | Short description | Types |
|---|---|---|
| E1 | Missing access in task dataset | `WRN` |
| E2 | Missing access in task code | `WRN`, `NTC` |
| E3 | Accessing a released region of a task dataset | `WRN` |
| E4 | Releasing a non-existent region of a task dataset | `WRN` |
| E5 | Missing taskwait | `WRN` |
| E6 | Missing region in the parent task dataset | `WRN` |
| E7 | Missing access in a child task dataset | `NTC` |

Long description:

* **E1** - The task doesn't declare an access to a memory region. This can cause races with other tasks that access the same memory. 
* **E2** - The task declares an access to a memory region that is not actually performed. While this cannot cause races with other tasks, it can still slow down the execution of the task if other tasks access the same memory.
* **E3** - Similar to E1, but the access was declared and later unregistered.
* **E4** - The task is trying to unregister an access that was never declared. This per-se doesn't create any parallelization error, but may suggest an error elsewhere.
* **E5** - The outermost task and some of its child task are not synchronized via a taskwait. This can cause a race between them.
* **E6** - The parent task doesn't declare any (weak) access to a memory region that is declared by one of its child task. This can cause a race between the child task and any other task that access the same memory from a different dependency domain.
* **E7** - A task declares a (weak) access to a memory region that is not declared as a (weak) access by any of its child tasks. While this cannot cause races with other tasks, it can still slow down the execution of the task if other tasks (included those from a different dependency domain) access the same memory.

# Verification annotations

To improve the effectiveness of our we introduce some ad-hoc verification annotations to be placed manually (or automatically) to disable analysis on well-defined regions of code.

Some examples for the two verification annotations are provided in the `examples/linter-ann-examples.c` directory. Additional examples are also shown below.

## The `oss lint` pragma

The `oss lint` pragma can be put inside task code to avoid analyzing the code wrapped by the directive. This is useful to mark code that is not relevant from the point of view of the parallelization. The pragma also allows to specify which memory operations are performed within a marked region of code, and to which memory addresses. The pragma accepts five main keywords: `in`, `out`, `inout`, `alloc`, and `free`. The first three are equivalent to those specified for a task dataset and allow the user to state which are the shared object read and/or written within the wrapped code. The last two are used to manually declare allocation and de-allocation of memory. These keywords can be used to summarize the behavior of the wrapped code in terms of its memory accesses, as shown in the examples below.

### Examples

We can inform the tool that a malloc-like allocation and a free-like deallocation have been performed. At the same time, we also disable instrumentation within the `my_malloc` and `my_free` functions, which is probably a good idea given that its implementation can be arbitrarily complex and can involve accesses to shared objects (e.g., locks) defined within the library that implements it.

Note that the tool already instruments automatically standard lib C `malloc` and `free` functions in a way which is equivalent to the following example, so there is no need to wrap their calls within a `oss lint` pragma.

	#pragma oss lint alloc(x[0:n])
	x = my_malloc(n * sizeof(int));

	#pragma oss lint free(*x)
	my_free(x);

We can inform the tool that we are using some API functions to read and/or write buffers in memory via I/O operations. The `MPI_Send` and `MPI_Recv` functions are respectively reading/writing _N_ bytes from/to memory. Not only the implementation of these functions can be arbitrary complex so as to slow down the analysis by orders of magnitude, but it can also affect negatively the accuracy of the generated report. Indeed, the synchronization mechanisms used within these functions are independent of the OmpSs-2 execution model and, as such, may require to access objects that aren't (and shouldn't be) declared as accesses in the task where these functions are invoked.

	#pragma oss lint in(sendbuf[0:size]) out(recvbuf[0:size])
	{
		MPI_Send(sendbuf, size, MPI_BYTE, dst,
			block_id+10, MPI_COMM_WORLD);
		MPI_Recv(recvbuf, size, MPI_BYTE, src,
			block_id+10, MPI_COMM_WORLD,
			MPI_STATUS_IGNORE);
	}

We can inform the tool about the presence of nested loops where the final effect is that of traversing a well-defined portion of an array for reading and/or writing. By marking this code with the pragma and summarizing its behavior we are saving the cost of analyzing a number of accesses that is proportional to the number of iterations. The analysis would still correctly detect all the accesses performed within the loop and there wouldn't be any accuracy concern, but the overall execution time would be much greater due to the cost of analysis.

	double A[N/TS][M/TS][TS][TS];
	
	#pragma oss lint out(A[i][j])
	for (long ii = 0; ii < TS; ii++)
		for (long jj = 0; jj < TS; jj++)
			A[i][j][ii][jj] = value;

## The `verified` attribute

The `verified` keyword can be passed to the `oss task` pragma to disable analysis at the level of whole tasks. It also comes with an optional boolean expression that is evaluated at run-time to decide whether memory tracing for the task will be disabled or not. This expression can be used to conditionally evaluate task instances that are more likely to be subject to programming errors.

### Examples

We can conditionally evaluate task instances related to boundary loop iterations:

	for (int i = 0; i < N; ++i)
		#pragma oss task verified(i > 0 && i < N-1)
			...

We can implement task-level sampling and instrument one out of _M_ task instances:

	for (int i = 0; i < N; ++i)
		#pragma oss task verified(i % M != 0)
			...

# Observations and remarks

* Currently, instrumentation is disabled when executing code from within the following libraries/images:
 	- All Nanos6 variants (included loader)
 	- Linux dynamic linker/loader
 	- Std lib C and C++
 	- Pthreads
 	- VDSO
* To date, the tool supports the following OmpSs-2 constructs:
	- `#pragma oss task`
		+ (`depend`) `in`, `out`, `inout`, `commutative`, `concurrent` attributes + `weak` prefix
		+ `wait` attribute
		+ `final` attribute
		+ `if` attribute
		+ `verified` attribute
	- `#pragma oss release`
		+ `in`, `out`, `inout` attributes
	- `#pragma oss taskwait`
		+ `in`, `out`, `inout` attributes
		+ `on` attribute
* OmpSs-2 features still not supported:
	- Task reductions
	- Taskfors
	- Critical sections
	- Atomic operations
