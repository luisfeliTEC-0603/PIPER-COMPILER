mov r1, r0
sumi r1, #1
shli r1, #16
mov r2, r0
sumi r2, #3
mov r3, r0
sumi r3, #7
mul r3, r2
stw r3, 0(r1)
low r4, 0(r1)
mov r5, r0
loop: sumi r5, #1
diffi r2, #1
jnz r2, loop
sum r4, r5
stwi r4, 4(r1)
done: j done
