# part1

## debug process

At first, we can use `sslittle-na-sstrix-objdump`

```shell
$IDIR/bin/sslittle-na-sstrix-objdump test1 > test1.asm
$IDIR/bin/sslittle-na-sstrix-objdump test2 > test2.asm
```

Then, it's easy to find what' wrong with the program:

this is assembly code of test1, we can find `0x00000061:10111300` in `<addOK>` can't be parsed by objdump:

```Assembly Language
00400240 <addOK>:
  ...
  400268:	34 00 00 00 	sw $19,28($29)
  40026c:	1c 00 13 1d 
  400270:	61 00 00 00 	0x00000061:10111300
  400274:	00 13 11 10 
  ...
```

And in `<biCount>` of test2.asm, we can also find `0x00000062:02030001`:

```Assembly Language
00400308 <bitCount>:
  ...
  400348:	06 00 00 00 	bne $2,$3,400370 <bitCount+0x68>
  40034c:	08 00 03 02 
  400350:	28 00 00 00 	lw $2,32($30)
  400354:	20 00 02 1e 
  400358:	62 00 00 00 	0x00000062:02030001
  40035c:	01 00 03 02 
  400360:	34 00 00 00 	sw $3,16($30)
  400364:	10 00 03 1e 
  400368:	01 00 00 00 	j 400388 <bitCount+0x80>
  40036c:	e2 00 10 00 
  400370:	28 00 00 00 	lw $2,32($30)
  400374:	20 00 02 1e 
  400378:	62 00 00 00 	0x00000062:02030000
  40037c:	00 00 03 02 
  ...
```

Obviously, there are two instructions. Now, we know that the opcode of `addOK` is `61`, opcode of `bitCount` is `62`

## how to support

As doc mentioned, `addOK` and `bitCount` are the same as `add` and `xori`, so we can simply modify them to support new instructions.

Luckily, we find a useful macro definition named `OVER` which can check whether the sum of two numbers is overflowed. So we can easily achieve this instruction by this:

```C
#define ADDOK_IMPL							\
  {									\
      SET_GPR(RD, !OVER(GPR(RS), GPR(RT)));				\
  }
DEFINST(ADDOK, 			0x61,
	"addok", 			"d,s,t",
	IntALU, 		F_ICOMP,
	DGPR(RD), DNA,		DGPR(RS), DGPR(RT), DNA)
```

But in `bitCount`, we can't find any useful macro definitions, so we need to use a loop to achieve this instruction:

```C
#define BITCOUNT_IMPL			    \
  {									\
    int n = GPR(RS);                \
    int c = 0;                      \
    int i;                    \
    for(i = 0; i < 32; i++) { \
      c += !(n>>i&1^UIMM);          \
    }               \
    SET_GPR(RT, c);  \
  }
DEFINST(BITCOUNT, 			0x62,
	"bitCount",			"t,s,u",
	IntALU,			F_ICOMP|F_IMM,
	DGPR(RT), DNA, 		DGPR(RS), DNA, DNA)
```

## dir structure

Actually, we only need to modify the `machine.def` to be a new version supporting those instructions, but if we want to achieve more features, we will modify `sim-fast.c`

* `machine.def` new version to support 
* `sim-fast.c` sim-fast source code
* `README.md` readme