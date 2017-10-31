NewTP, a file transfer protocol
-------------------------------

This repository contains the text and reference implementation for NewTP,
a protocol for transferring files over a single TCP connection.

The __`tex`__ subdirectory contains thesis text in LaTeX format.

The root directory contains the source code. Original readme for the implementation
is in `README.newtp`.

`code-overview.txt` contains the programmer documentation. Code is also heavily commented.

Important to note is that __this is not production-ready code__. It performs insecure
TLS authentication, has very low performance, and most likely contains severe bugs.
__DO NOT USE__ in the real world!

Text of the thesis is released under [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
Source code is released under [GPL 3.0 and up](https://www.gnu.org/licenses/gpl-3.0.en.html).
However, the protocol is designed to be easy to implement, and you're urged to make your own implementation.
