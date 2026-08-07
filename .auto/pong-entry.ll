; --- LLVM trace before O2: ADDR200 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR200() {
  br label %1

1:                                                ; preds = %0
  %2 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %3 = add i64 %2, 1
  store i64 %3, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i8 2, ptr inttoptr (i64 103576183157018 to ptr), align 1
  br label %4

4:                                                ; preds = %1
  %5 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i8 12, ptr inttoptr (i64 103576183157019 to ptr), align 1
  br label %7

7:                                                ; preds = %4
  %8 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %9 = add i64 %8, 1
  store i64 %9, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i8 63, ptr inttoptr (i64 103576183157020 to ptr), align 1
  br label %10

10:                                               ; preds = %7
  %11 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i8 12, ptr inttoptr (i64 103576183157021 to ptr), align 1
  br label %13

13:                                               ; preds = %10
  %14 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %15 = add i64 %14, 1
  store i64 %15, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i16 746, ptr inttoptr (i64 103576183156994 to ptr), align 2
  br label %16

16:                                               ; preds = %13
  %17 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %18 = add i64 %17, 1
  store i64 %18, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i16 522, ptr inttoptr (i64 103576183156992 to ptr), align 2
  call void @draw()
  br label %19

19:                                               ; preds = %16
  %20 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %21 = add i64 %20, 1
  store i64 %21, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i16 524, ptr inttoptr (i64 103576183156992 to ptr), align 2
  call void @draw()
  br label %22

22:                                               ; preds = %19
  %23 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %24 = add i64 %23, 1
  store i64 %24, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i8 0, ptr inttoptr (i64 103576183157022 to ptr), align 1
  br label %25

25:                                               ; preds = %22
  %26 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 4
  %27 = add i64 %26, 1
  store i64 %27, ptr inttoptr (i64 103576183160856 to ptr), align 4
  store i16 528, ptr inttoptr (i64 103576183156992 to ptr), align 2
  call void @call()
  ret void
}

declare void @draw()

declare void @call()
; --- LLVM trace after O2: ADDR200 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR200() local_unnamed_addr {
  %1 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %2 = add i64 %1, 1
  store i64 %2, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i8 2, ptr inttoptr (i64 103576183157018 to ptr), align 2
  %3 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %4 = add i64 %3, 1
  store i64 %4, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i8 12, ptr inttoptr (i64 103576183157019 to ptr), align 1
  %5 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i8 63, ptr inttoptr (i64 103576183157020 to ptr), align 4
  %7 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %8 = add i64 %7, 1
  store i64 %8, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i8 12, ptr inttoptr (i64 103576183157021 to ptr), align 1
  %9 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %10 = add i64 %9, 1
  store i64 %10, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i16 746, ptr inttoptr (i64 103576183156994 to ptr), align 2
  %11 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i16 522, ptr inttoptr (i64 103576183156992 to ptr), align 256
  tail call void @draw()
  %13 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %14 = add i64 %13, 1
  store i64 %14, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i16 524, ptr inttoptr (i64 103576183156992 to ptr), align 256
  tail call void @draw()
  %15 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %16 = add i64 %15, 1
  store i64 %16, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i8 0, ptr inttoptr (i64 103576183157022 to ptr), align 2
  %17 = load i64, ptr inttoptr (i64 103576183160856 to ptr), align 8
  %18 = add i64 %17, 1
  store i64 %18, ptr inttoptr (i64 103576183160856 to ptr), align 8
  store i16 528, ptr inttoptr (i64 103576183156992 to ptr), align 256
  tail call void @call()
  ret void
}

declare void @draw() local_unnamed_addr

declare void @call() local_unnamed_addr
V0 = 0x60
V1 = 0x00
V2 = 0x00
V3 = 0x00
V4 = 0x29
V5 = 0x00
V6 = 0x03
V7 = 0x00
V8 = 0x02
V9 = 0x00
VA = 0x02
VB = 0x0C
VC = 0x3F
VD = 0x0C
VE = 0x00
VF = 0x00
3 traces executed
$pc = 0x021A
$addr = 0x0000
stack depth = 0
delay = 96
sound = 0
engine = llvm
seed = 20240101
keys = rotate
retired = 1026
compiled = 4
flushes = 0
clock = 1026
display = 0xb3835d2f18ade747
elapsed = 0.015 s
rate = 0.07 Minsn/s
