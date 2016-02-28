Instruction decoder microbenchmark suite
========================================

This is a microbenchmark suite designed to test the power consumption of the x86-64 instruction decoders. The code is specifically designed to run on Intel processors based on the Sandy Bridge and later architectures. We have published our results in the following paper:

Hirki, M., Ou, Z., Khan, K.N., Nurminen, J.K., Niemi, T., "Empirical Study of Power Consumption of x86-64 Instruction Decoder", USENIX Workshop on Cool Topics in Sustainable Data Centers (CoolDC '16) (to appear)

Required dependencies:
 - The PAPI (Performance Application Programming Interface) library, http://icl.cs.utk.edu/papi/

Compiling:
 - Type "make".

Tested to compile and run on Scientific Linux 6.

Author: Mikael Hirki <mikael.hirki@gmail.com>

Copyright (c) 2015 Helsinki Institute of Physics

This software is distributed under the terms of the "MIT license". Read COPYING.txt for more details.
