A run-time tool to detect parallelization errors in OmpSs-2 applications.

# Description
Programming models for task-based parallelization based on compile-time directives are very effective at uncovering the parallelism available in HPC applications. However, correctly annotating complex applications is an error-prone process that hinders the general adoption of these models. OmpSs-2 Linter is a debugging/linting tool that takes as input an application parallelized using the OmpSs-2 programming model and reports all the potential parallelization errors encountered while tracing the application. Programmers can then use the tool to make sure that nothing was left behind during the annotation process.

Technically, OmpSs-2 Linter is a run-time dynamic binary instrumentation tool built on top of Intel PIN. When an OmpSs-2 application is run through this tool, memory accesses issued by tasks are recorded and temporarily saved to an in-memory storage area. When a task completes, the recorded memory accesses issued by that task are processed and compared with task-level information (e.g., dependencies) to check for potential parallelization errors. For each of such errors, a report message is generated to inform the user about the problem. The message comes with additional contextual information such as

address and size of the mismatching memory access (if any) along with its access mode (i.e., read or write),
name of the involved dependency (if any) with the expected directionality (i.e., `in`, `out`, or `inout`),
variable name (if found),
task invocation point in the source code (i.e., line in the corresponding file).

# Requirements

Currently, OmpSs-2 Linter only runs on GNU/Linux systems. It also requires the following software and versions:

| Software | Version |
|---|---|
| OmpSs-2 | >= [2020.06](https://github.com/bsc-pm/ompss-2-releases/tree/2020.06) |
| Intel PIN | [3.11](https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.11-97998-g7ecce2dac-gcc-linux.tar.gz) |

_NOTE_: It is necessary to compile Nanos6 (in the OmpSs-2 package) with the `--enable-lint-instrumentation` and the `--with-symbol-resolution=ifunc` configure flags.

# Download

Download the latest OmpSs-2 Linter release at [this link](https://github.com/bsc-pm/ompss-2-linter/archive/master.zip).

# Installation

In order to install OmpSs-2 Linter, we need to inform the install script about the location of the Intel PIN binary:

	export PIN_ROOT=<path to PIN root directory>

Then, the install script can be invoked as follows:

	./install.sh

_NOTE_: To run this step, `libelf​` `>= 0.8.13` (or `elfutils`​ `>= 0.158`) is required.

# Usage

In order to execute OmpSs-2 Linter, we need to inform it about the location of the Intel PIN binary:

	export PIN_ROOT=<path to PIN root directory>

Then, OmpSs-2 Linter can be executed via the launcher:

	./ompss-2-linter.sh <executable>

Where `<executable>` is the path to the OmpSs-2 application that needs to be verified.

_NOTE_: In order to fully exploit the tool's capabilities, the OmpSs-2 application must be compiled with the following flags:

* (C/C++ compiler) `-O0` to disable compiler optimizations
* (C/C++ compiler)`-g` to emit debug information
* (Mercurium) `--line-markers` to map lines in the original source program to lines in the transformed source program

## Parameters

The launcher accepts some additional parameters:

* `-d <0..6>` (default: `0`)
	- This parameter specifies the debugging level. The greater this value, the more verbose is the tool. Notice that this parameter does not affect the reporting of parallelization errors.
* `-r <NTC|WRN>` (default: `WRN`)
	- This parameter specifies the kind of parallelization errors reported by the tool. `NTC` means that both notices and warnings are reported. `WRN` means that only warnings are reported. See "Errors" for an explanation of these two classes of errors.
* `-e`
   - This parameter enables the generation of an extended report, showing an error for each instance of a task rather than for each task in source code. This option typically increases the report size by orders of magnitude. It is useful to debug an application when the initial report is not sufficiently detailed to understand the cause of the error.
* `-a <0..1>` (default: `1`)
	- This parameter specifies the aggregation level for the accesses traced by the tool.
	- The value `0` means that every single access that is performed during execution is treated independently. This option generates the least amount of false positives at the expense of the highest execution overhead.
	- The value `1` means that all accesses that come from the same instruction and that target addresses contiguous in memory are merged into one single entry in the trace. This option can speed-up execution in the case of multi-dimensional array traversals but sometimes generates false positives. See "Errors" for an explanation of the false positives detected while aggregating accesses.
* `-p <seconds>` (default: `0`)
	- This parameter is used to run the tool in debug mode. The debug mode pauses the execution of the tool during the first `<seconds>` seconds to allow to attach a separate GDB session to it. It is only useful to debug errors in the Linter itself, rather than errors in the application.
* `-o <outputs_directory>` (default: `linter-data`)
	- This parameter specifies the path to the directory used by the tool to save output data (not meant to be read by the user). It is useful when running multi-process programs, or MPI programs on a shared distributed filesystem, to make sure that each process writes on a different directory.

For example, to verify an application while generating the smallest useful amount of information in the report, we can launch the tool as follows:

	./ompss-2-linter.sh -d 0 -a 1 -r WRN <executable>

To generate the maximum useful amount of information, we can launch the tool with the following parameters:

	./ompss-2-linter.sh -d 6 -a 0 -r NTC -e <executable>

When running the tool with MPI applications, and assuming a system with a batch scheduler such as SLURM, the tool can be launched through the following `sbatch` script template:

	#!/bin/bash
	
	# SBATCH options...
	
	srun ./ompss-2-linter.sh -o linter-data_$SLURM_NODEID-$SLURM_PROCID

# Errors

To date, the tool can detect several potential errors, grouped into

* **warnings** (`WRN`), which may negatively affect the _correctness_ of the parallelization, and
* **notices** (`NTC`), which may negatively affect the _performance_ of the parallelization, or suggest the presence of other errors elsewhere in the program.

Note that these errors are only _potential_, meaning that they may be due to either bugs or explicit design choices by the programmer. Please refer to the [rules of the OmpSs-2 programming model](https://pm.bsc.es/ftp/ompss-2/doc/spec/) to better understand the implications of each error.

Some examples for each class and type of errors are provided in the `examples/linter-main-examples.c` directory.

| Class | Short description | Types |
|---|---|---|
| E1 | Missing access in task dataset | `WRN` |
| E2 | Missing access in task code | `WRN`, `NTC` |
| E3 | Missing access in the parent task dataset | `WRN` |
| E4 | Missing access in a child task dataset | `NTC` |
| E5 | Missing taskwait | `WRN` |
| E6 | Missing access in task dataset (after release) | `WRN` |
| E7 | Missing access in task dataset (upon release) | `NTC` |

* **E1** - The task does not declare a memory access to a region that is actually accessed. This error can cause races with other tasks that access the same memory. 
* **E2** - The task declares a memory access to a region that is not actually accessed. While this error cannot cause races with other tasks, it can still slow down the program's execution, if other tasks access the same memory.
* **E3** - The parent task does not declare any weak memory access to a region declared by one of its child tasks. This error can cause a race between the child task and any other task that accesses the same region from a different dependency domain.
* **E4** - The task declares a weak memory access to a region that is not declared as a (weak) access by any of its child tasks. This error does not create any parallelization error but may suggest an error elsewhere.
* **E5** - The outermost task and some of its child tasks are not synchronized via a taskwait. This error can cause a race between them.
* **E6** - The task unregisters a memory access to a region that is later accessed. Similar to E1, this error can cause races with other tasks that access the same memory.
* **E7** - The task is trying to unregister an access that was never declared. This error does not create any parallelization error but may suggest an error elsewhere.

# Verification annotations

To improve the effectiveness of our tool, we introduce some ad-hoc verification annotations to be placed manually (or automatically) to disable analysis on well-defined regions of code.

Some examples for the two verification annotations are provided in the `examples/linter-ann-examples.c` directory. Additional examples are also shown below.

## The `oss lint` pragma

The `oss lint` pragma can be put inside the body of a task to avoid analyzing the code wrapped by the directive. This directive is useful to mark code that is not relevant from the parallelization point of view. The pragma also allows specifying which memory operations are performed within a marked region of code, and to which memory addresses. The pragma accepts five main keywords: `in`, `out`, `inout`, `alloc`, and `free`. The first three are equivalent to those specified for a task dataset and allow the user to state which are the shared objects read or written within the wrapped code. The remaining two are used to declare allocation and deallocation of memory. These keywords can be used to summarize the behavior of the wrapped code in terms of its memory accesses, as shown in the examples below.

### Examples

We can inform the tool that malloc-like and free-like operations have been performed. At the same time, we also disable instrumentation within the `my_malloc` and `my_free` functions, which is probably a good idea given that its implementation can be arbitrarily complex and can involve accesses to shared objects (e.g., locks) defined within the library that implements it.

Note that the tool already instruments automatically standard lib C `malloc` and `free` functions in a way which is equivalent to the following example, so there is no need to wrap their calls within an `oss lint` pragma.

	#pragma oss lint alloc(x[0:n])
	x = my_malloc(n * sizeof(int));

	#pragma oss lint free(*x)
	my_free(x);

We can inform the tool that we are using some API functions to read or write buffers in memory via I/O operations. The `MPI_Send` and `MPI_Recv` functions are respectively reading/writing _N_ bytes from/to memory. Not only can the implementation of these functions be arbitrary complex to slow down the analysis by orders of magnitude, but it can also negatively affect the accuracy of the generated report. Indeed, the synchronization mechanisms used within these functions are independent of the OmpSs-2 execution model and, as such, may require to access objects that are not (and should not be) declared as accesses in the task where these functions are invoked.

	#pragma oss lint in(sendbuf[0:size]) out(recvbuf[0:size])
	{
		MPI_Send(sendbuf, size, MPI_BYTE, dst,
			block_id+10, MPI_COMM_WORLD);
		MPI_Recv(recvbuf, size, MPI_BYTE, src,
			block_id+10, MPI_COMM_WORLD,
			MPI_STATUS_IGNORE);
	}

We can inform the tool about the presence of nested loops where the final effect is that of traversing a well-defined portion of an array for reading or writing. By marking this code with the pragma and summarizing its behavior, we are saving the cost of analyzing a number of accesses that are proportional to the number of iterations. The analysis would still correctly detect all the accesses performed within the loop, and there would not be any accuracy concern, but the overall execution time would be much higher due to the cost of analysis.

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

# Static analysis

In order to improve the overhead and the accuracy of the tool, verification annotations can also be automatically placed by a static analysis phase, run while compiling the application with Mercurium. This phase analyzes all source code, looking for regions to annotate with `oss lint` pragmas, and tasks declarations to annotate with the `verified` attribute. The effectiveness of this pass depends on the quality and structure of source code. This static phase can also leverage the verification annotations placed by the user in an attempt to extend them to larger code regions.

To enable this static analysis pass while compiling code with Mercurium, use one of the following flags:

* `oss-lint-tasks` to ask Mercurium to automatically insert `verified` attributes (if possible);
* `oss-lint` to ask Mercurium to automatically insert `oss lint` pragmas and `verified` attributes (if possible).

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
* OmpSs-2 features not yet supported:
	- Task reductions
	- Taskfors
	- Critical sections
	- Atomic operations

# References

This tool has been officially presented at the Euro-Par 2020 conference:

> Economo S., Royuela S., Ayguadé E., Beltran V. (2020) A Toolchain to Verify the Parallelization of OmpSs-2 Applications. In: Malawski M., Rzadca K. (eds) Euro-Par 2020: Parallel Processing. Euro-Par 2020. Lecture Notes in Computer Science, vol 12247. Springer, Cham. https://doi.org/10.1007/978-3-030-57675-2_2

* [Video of the presentation](https://youtu.be/pMNn7sU6S2g)
* [Conference proceedings](https://link.springer.com/chapter/10.1007/978-3-030-57675-2_2)
