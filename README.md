# linux shell mock

A mock command line client for linux. 

Parses and launches given commands that can be found on path.
Implements |, ||, &, &&, > operators.

Decide to take on this little project while trying to learn the C language.
It is still work in progress. Any feedback and citique would be highly appreciated.

## usage
Since at this point it is still composed of one source file I didn't really see a need to create makefile.

It requires no unusual packages and should compile and run if you have linux with build-essential installed.

Tested on x86 and ARM with gcc and clang.
