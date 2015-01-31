CHocoParse
==========
A parser for the TypeSafe config format HOCON in C with minimal dependencies.  For HOCON reference, see: https://github.com/typesafehub/config.

This is a work in progress for my own edification and not ready for use!

Implementation Notes
====================
* The input is always UTF-8 (switch to modified UTF-8?)
* Any invalid UTF-8 such as overlong encodings will be rejected.
