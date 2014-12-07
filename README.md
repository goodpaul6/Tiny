Tiny is an implementation of the Tiny programming language as specified by this wikipedia page:
http://en.wikipedia.org/wiki/Tiny_programming_language

It compiles a program to it's own bytecode and then executes said bytecode.

The language itself is very simple, and not very practical at all, but is very easy to implement.

The following is an example program:
''' 
begin
	read x y z end
	read x + y + z * 3 end
end
''' 
This will read 3 numbers from stdin and write their sum multiplied by 3 to stdout.
There is no operator precedence in the language; as such, parentheses must be used where an operation 
must precede another.
