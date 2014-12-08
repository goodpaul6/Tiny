Tiny is an implementation of the Tiny programming language as specified by this wikipedia page (but very much extended):
http://en.wikipedia.org/wiki/Tiny_programming_language

It compiles a program to it's own bytecode and then executes said bytecode.

The following is an example program:

```
begin
	0						# local variable slot 0	
	read x y z end			# read 3 numbers from stdin
	$0 = x + y + z			# set local slot 0 to x + y + z
	write $0 end			# write value in local slot 0 to stdout
end
```

This program will write the sum of 3 numbers which it reads from stdin to stdout.

Another example program:

```
begin 
	proc fact							# compute the factorial of a number
		if $-1 == 0						# the arguments are numbered from $-1 to $-n where n is the number of arguments (-1 is the last, -n is the first)
			return 1					# exit the function returning 1 if the argument passed in is 0
		end
		
		return $-1 * fact($-1 - 1)		# otherwise, compute the factorial...
	end
	
	write fact(5) end					# compute factorial of 5 and write to stdout
end
```

This program willl write the factorial of 5 (120) to stdout

At the moment, there are no named arguments or named locals, but that is planned for the future.
There is also no other data type aside from double. Again, more is planned. At the moment, it has 
very simple code, a large part of which is simply just for debugging when necessary.
