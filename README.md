# A Simply Turing Machine Simulator
## instruction card format
```
<N>
<now> <n>
<read> <write> <move> <next>
<read> <write> <move> <next>
...
<now> <n>
<read> <write> <move> <next>
...
```
- read/write symbol: 0 ~ 255
- move direction: L, R, S
- now/next state: 0 ~ 2147483647
- N: number of states, and states must be in [0, N-1], and N must be in [1, 2147483647]
- n: number of instructions for the state, and n must be in [0, 255]
- if there is no instruction for a state, the machine will halt when it enters that state
- entry point is state 0, and the tape is infinite in both directions
## tape format
- the leftmost cell is the current position of the tape head
- The initial tape length must be in [0, 2147483647]
### format 1
```
<cell> <cell> <cell> ...
```
or
```
<cell>,<cell>,<cell> ...
```
or
```
<cell>
<cell>
<cell> ...
```
- cell value: 0 ~ 255
- file extension: .txt
### format 2
```
<cell><cell><cell>...
```
- cell value: character with ASCII code 0 ~ 255
- file extension: .bin
## example
```
2

0 2
0 1 R 1
1 0 L 1

1 1
0 1 R 0
```
- if now state is 0 and read 0, the machine will write 1, move right, and change state to 1
- if now state is 0 and read 1, the machine will write 0, move left, and change state to 1
- if now state is 1 and read 0, the machine will write 1, move right, and change state to 0
- in this example, if now state is 1 and read 1, the machine will halt, because there is no instruction for state 1 and read 1
