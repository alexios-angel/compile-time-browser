// RUN: python3 %S/entry.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --work %t

// A named entry function that declares a class prepares with its script wrapper
// unchanged; DOM calls and entry parameters the code reads are refused.
