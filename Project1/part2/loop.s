	.global __start
__start:
        li      $sp,0x7fff0000  # initialize stack pointer
        move    $fp,$sp         # and frame pointer
        sw      $0,16($fp)
        li      $6,0
$L1:
        lw      $2,16($fp)      # loop head
        slt     $3,$2,10
        bne     $3,$0,$L2
        j       $L3
$L2:
        move    $4,$2           # loop body
        sll     $5,$4,2
        subu    $4,$5,13
        add     $6,$6,$4
        lw      $3,16($fp)
        addu    $2,$3,1
        move    $3,$2
        sw      $3,16($fp)
        j       $L1
$L3:
        li      $2,1            # end the program
		and		$6, 0xffff
		sw		$6,16($fp)			# store the magic number
        syscall 


