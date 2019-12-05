# hmalloc
A bucket-based memory allocator designed to be faster than the system malloc()\*.

\*(*sometimes*)

# Results

LOCAL:
OS: Arch Linux x86_64
Processor: Intel i7-8550U
Cores: 8
RAM: 15.7GB

|    | IVEC | LIST  |
|:--:|:----:|:-----:|
|SYS | 0.01 |  0.02 |
|PAR | 0.00 |  0.01 |
|HW7 | 0.94 | 22.82 |

Written by Connor Northway and Jack Leightcap