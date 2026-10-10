/* Force-included when building jxrlib (see the Makefile / CMakeLists.txt).
 * jxrlib was written for Windows and leans on two things other compilers
 * don't declare by default. */
#include <wchar.h>
unsigned int _byteswap_ulong(unsigned int bits);
