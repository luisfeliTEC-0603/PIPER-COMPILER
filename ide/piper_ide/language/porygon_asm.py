"""Mnemónicos visuales de porygon_assembler/mnemonics.c, sin ensamblador Python."""

MNEMONICS = frozenset("""nop mov sum sumi diff diffi and andi or ori xor xori not
shl shli shr shri sar sari mul muli mulh mulhi gtn ltn eor nez
low lowi lob lobi lobu lobui loh lohi lohu lohui stw stwi stb stbi sth sthi
j ji jz jzi jnz jnzi jr auth write feistel pwd rvk lopw""".split())
