#ifndef TINY_COMPILE_OPTIONS_H_
#define TINY_COMPILE_OPTIONS_H_

// Add compiler
#define TINY_COMPILER

// Add VM utils functions
#define TINY_VM_UTILS

#ifdef TINY_COMPILER
    #ifndef TINY_VM_UTILS
        #define TINY_VM_UTILS
    #endif
#endif

#endif /* COMPILE_OPTIONS_H_ */
