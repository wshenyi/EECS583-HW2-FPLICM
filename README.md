# Frequent Path Loop Invariant Code Motion

Homework2 for EECS 583

## Usage

First `cd benchmarks` and run all benchmarks `./check.sh`

## Result

Time used after using performance pass in one execution. To get a correct result, we need to run at least two times. The left time is **unoptimized** runtime and the right time is **optimized** runtime.

```shell
hw2correct1 PASSED real 0m0.001s => real 0m0.001s
hw2correct2 PASSED real 0m0.001s => real 0m0.001s
hw2correct3 PASSED real 0m0.001s => real 0m0.001s
hw2correct4 PASSED real 0m0.002s => real 0m0.001s
hw2correct5 PASSED real 0m0.001s => real 0m0.001s
hw2correct6 PASSED real 0m0.002s => real 0m0.001s
hw2perf1    PASSED real 0m6.899s => real 0m4.706s
hw2perf2    PASSED real 0m9.448s => real 0m6.450s
hw2perf3    PASSED real 0m24.043s => real 0m10.328s
hw2perf4    PASSED real 0m30.221s => real 0m13.396s
```

## Example

Let's use benchmark6 as an example.

### Original CFG

[file](benchmarks/correctness/dot/hw2correct6.cfg.pdf)  

<image src="pic/origin.png" width=700px height=1000px>

### Optimized CFG

[file](benchmarks/correctness/dot/hw2correct6.fplicm.cfg.pdf)  

<image src="pic/opt.png" width=700px height=1000px>