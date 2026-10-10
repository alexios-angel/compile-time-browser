// RUN: python3 %S/entry.py --translate ctjs-translate --opt ctjs-opt --node %node --reference %native_reference --work %t

// Explicit class entries retain their inert declaration wrapper. DOM calls and
// observed host parameters still need a joint source/effect and receiver proof.
