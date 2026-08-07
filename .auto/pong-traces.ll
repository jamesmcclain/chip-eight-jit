; --- LLVM trace before O2: ADDR200 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR200() {
  br label %1

1:                                                ; preds = %0
  %2 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %3 = add i64 %2, 1
  store i64 %3, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 2, ptr inttoptr (i64 108535933088026 to ptr), align 1
  br label %4

4:                                                ; preds = %1
  %5 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 12, ptr inttoptr (i64 108535933088027 to ptr), align 1
  br label %7

7:                                                ; preds = %4
  %8 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %9 = add i64 %8, 1
  store i64 %9, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 63, ptr inttoptr (i64 108535933088028 to ptr), align 1
  br label %10

10:                                               ; preds = %7
  %11 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 12, ptr inttoptr (i64 108535933088029 to ptr), align 1
  br label %13

13:                                               ; preds = %10
  %14 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %15 = add i64 %14, 1
  store i64 %15, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 746, ptr inttoptr (i64 108535933088002 to ptr), align 2
  br label %16

16:                                               ; preds = %13
  %17 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %18 = add i64 %17, 1
  store i64 %18, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 522, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %19

19:                                               ; preds = %16
  %20 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %21 = add i64 %20, 1
  store i64 %21, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 524, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %22

22:                                               ; preds = %19
  %23 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %24 = add i64 %23, 1
  store i64 %24, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 0, ptr inttoptr (i64 108535933088030 to ptr), align 1
  br label %25

25:                                               ; preds = %22
  %26 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %27 = add i64 %26, 1
  store i64 %27, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 528, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @call()
  ret void
}

declare void @draw()

declare void @call()
; --- LLVM trace after O2: ADDR200 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR200() local_unnamed_addr {
  %1 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %2 = add i64 %1, 1
  store i64 %2, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 2, ptr inttoptr (i64 108535933088026 to ptr), align 2
  %3 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %4 = add i64 %3, 1
  store i64 %4, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 12, ptr inttoptr (i64 108535933088027 to ptr), align 1
  %5 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 63, ptr inttoptr (i64 108535933088028 to ptr), align 4
  %7 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %8 = add i64 %7, 1
  store i64 %8, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 12, ptr inttoptr (i64 108535933088029 to ptr), align 1
  %9 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %10 = add i64 %9, 1
  store i64 %10, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 746, ptr inttoptr (i64 108535933088002 to ptr), align 2
  %11 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 522, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %13 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %14 = add i64 %13, 1
  store i64 %14, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 524, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %15 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %16 = add i64 %15, 1
  store i64 %16, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 0, ptr inttoptr (i64 108535933088030 to ptr), align 2
  %17 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %18 = add i64 %17, 1
  store i64 %18, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 528, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @call()
  ret void
}

declare void @draw() local_unnamed_addr

declare void @call() local_unnamed_addr
; --- LLVM trace before O2: ADDR2D4 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR2D4() {
  br label %1

1:                                                ; preds = %0
  %2 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %3 = add i64 %2, 1
  store i64 %3, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 754, ptr inttoptr (i64 108535933088002 to ptr), align 2
  br label %4

4:                                                ; preds = %1
  %5 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 726, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @store_bcd()
  ret void
}

declare void @store_bcd()
; --- LLVM trace after O2: ADDR2D4 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR2D4() local_unnamed_addr {
  %1 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %2 = add i64 %1, 1
  store i64 %2, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 754, ptr inttoptr (i64 108535933088002 to ptr), align 2
  %3 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %4 = add i64 %3, 1
  store i64 %4, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 726, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @store_bcd()
  ret void
}

declare void @store_bcd() local_unnamed_addr
; --- LLVM trace before O2: ADDR2D8 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR2D8() {
  br label %1

1:                                                ; preds = %0
  %2 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %3 = add i64 %2, 1
  store i64 %3, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 728, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @restore_registers()
  br label %4

4:                                                ; preds = %1
  %5 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %7 = load i8, ptr inttoptr (i64 108535933088017 to ptr), align 1
  %8 = zext i8 %7 to i16
  %9 = mul i16 %8, 5
  store i16 %9, ptr inttoptr (i64 108535933088002 to ptr), align 2
  br label %10

10:                                               ; preds = %4
  %11 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 20, ptr inttoptr (i64 108535933088020 to ptr), align 1
  br label %13

13:                                               ; preds = %10
  %14 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %15 = add i64 %14, 1
  store i64 %15, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 0, ptr inttoptr (i64 108535933088021 to ptr), align 1
  br label %16

16:                                               ; preds = %13
  %17 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %18 = add i64 %17, 1
  store i64 %18, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 736, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %19

19:                                               ; preds = %16
  %20 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %21 = add i64 %20, 1
  store i64 %21, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %22 = load i8, ptr inttoptr (i64 108535933088020 to ptr), align 1
  %23 = add i8 %22, 21
  store i8 %23, ptr inttoptr (i64 108535933088020 to ptr), align 1
  br label %24

24:                                               ; preds = %19
  %25 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %26 = add i64 %25, 1
  store i64 %26, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %27 = load i8, ptr inttoptr (i64 108535933088018 to ptr), align 1
  %28 = zext i8 %27 to i16
  %29 = mul i16 %28, 5
  store i16 %29, ptr inttoptr (i64 108535933088002 to ptr), align 2
  br label %30

30:                                               ; preds = %24
  %31 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %32 = add i64 %31, 1
  store i64 %32, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 742, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %33

33:                                               ; preds = %30
  %34 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %35 = add i64 %34, 1
  store i64 %35, ptr inttoptr (i64 108535933091864 to ptr), align 4
  call void @retern()
  ret void
}

declare void @restore_registers()

declare void @draw()

declare void @retern()
; --- LLVM trace after O2: ADDR2D8 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR2D8() local_unnamed_addr {
  %1 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %2 = add i64 %1, 1
  store i64 %2, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 728, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @restore_registers()
  %3 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %4 = add i64 %3, 1
  store i64 %4, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %5 = load i8, ptr inttoptr (i64 108535933088017 to ptr), align 1
  %6 = zext i8 %5 to i16
  %7 = mul nuw nsw i16 %6, 5
  store i16 %7, ptr inttoptr (i64 108535933088002 to ptr), align 2
  %8 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %9 = add i64 %8, 1
  store i64 %9, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 20, ptr inttoptr (i64 108535933088020 to ptr), align 4
  %10 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %11 = add i64 %10, 1
  store i64 %11, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 0, ptr inttoptr (i64 108535933088021 to ptr), align 1
  %12 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %13 = add i64 %12, 1
  store i64 %13, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 736, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %14 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %15 = add i64 %14, 1
  store i64 %15, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %16 = load i8, ptr inttoptr (i64 108535933088020 to ptr), align 4
  %17 = add i8 %16, 21
  store i8 %17, ptr inttoptr (i64 108535933088020 to ptr), align 4
  %18 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %19 = add i64 %18, 1
  store i64 %19, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %20 = load i8, ptr inttoptr (i64 108535933088018 to ptr), align 2
  %21 = zext i8 %20 to i16
  %22 = mul nuw nsw i16 %21, 5
  store i16 %22, ptr inttoptr (i64 108535933088002 to ptr), align 2
  %23 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %24 = add i64 %23, 1
  store i64 %24, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 742, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %25 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %26 = add i64 %25, 1
  store i64 %26, ptr inttoptr (i64 108535933091864 to ptr), align 8
  tail call void @retern()
  ret void
}

declare void @restore_registers() local_unnamed_addr

declare void @draw() local_unnamed_addr

declare void @retern() local_unnamed_addr
; --- LLVM trace before O2: ADDR212 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR212() {
  br label %1

1:                                                ; preds = %0
  %2 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %3 = add i64 %2, 1
  store i64 %3, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 3, ptr inttoptr (i64 108535933088022 to ptr), align 1
  br label %4

4:                                                ; preds = %1
  %5 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 2, ptr inttoptr (i64 108535933088024 to ptr), align 1
  br label %7

7:                                                ; preds = %4
  %8 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %9 = add i64 %8, 1
  store i64 %9, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 96, ptr inttoptr (i64 108535933088016 to ptr), align 1
  br label %10

10:                                               ; preds = %7
  %11 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %13 = load i8, ptr inttoptr (i64 108535933088016 to ptr), align 1
  call void @sync_timers()
  store i8 %13, ptr inttoptr (i64 108535933083883 to ptr), align 1
  br label %14

14:                                               ; preds = %31, %10
  %15 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %16 = add i64 %15, 1
  store i64 %16, ptr inttoptr (i64 108535933091864 to ptr), align 4
  call void @sync_timers()
  %17 = load i8, ptr inttoptr (i64 108535933083883 to ptr), align 1
  store i8 %17, ptr inttoptr (i64 108535933088016 to ptr), align 1
  br label %18

18:                                               ; preds = %14
  %19 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %20 = add i64 %19, 1
  store i64 %20, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %21 = load i8, ptr inttoptr (i64 108535933088016 to ptr), align 1
  %22 = icmp eq i8 %21, 0
  br i1 %22, label %23, label %24

23:                                               ; preds = %18
  store i16 544, ptr inttoptr (i64 108535933088000 to ptr), align 2
  br label %35

24:                                               ; preds = %18
  %25 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %26 = add i64 %25, 1
  store i64 %26, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %27 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %28 = load volatile i64, ptr inttoptr (i64 108535933079568 to ptr), align 4
  %29 = icmp sge i64 %27, %28
  br i1 %29, label %30, label %31

30:                                               ; preds = %24
  call void @bench_safepoint()
  br label %31

31:                                               ; preds = %30, %24
  store i16 538, ptr inttoptr (i64 108535933088000 to ptr), align 2
  %32 = load volatile i8, ptr inttoptr (i64 108535933083816 to ptr), align 1
  %33 = icmp ne i8 %32, 0
  br i1 %33, label %34, label %14

34:                                               ; preds = %31
  ret void

35:                                               ; preds = %23
  %36 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %37 = add i64 %36, 1
  store i64 %37, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 544, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @random_byte()
  br label %38

38:                                               ; preds = %35
  %39 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %40 = add i64 %39, 1
  store i64 %40, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %41 = load i8, ptr inttoptr (i64 108535933088023 to ptr), align 1
  %42 = add i8 %41, 8
  store i8 %42, ptr inttoptr (i64 108535933088023 to ptr), align 1
  br label %43

43:                                               ; preds = %38
  %44 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %45 = add i64 %44, 1
  store i64 %45, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 -1, ptr inttoptr (i64 108535933088025 to ptr), align 1
  br label %46

46:                                               ; preds = %43
  %47 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %48 = add i64 %47, 1
  store i64 %48, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 752, ptr inttoptr (i64 108535933088002 to ptr), align 2
  br label %49

49:                                               ; preds = %46
  %50 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %51 = add i64 %50, 1
  store i64 %51, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 552, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %52

52:                                               ; preds = %49
  %53 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %54 = add i64 %53, 1
  store i64 %54, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 746, ptr inttoptr (i64 108535933088002 to ptr), align 2
  br label %55

55:                                               ; preds = %52
  %56 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %57 = add i64 %56, 1
  store i64 %57, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 556, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %58

58:                                               ; preds = %55
  %59 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %60 = add i64 %59, 1
  store i64 %60, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 558, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @draw()
  br label %61

61:                                               ; preds = %58
  %62 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %63 = add i64 %62, 1
  store i64 %63, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i8 1, ptr inttoptr (i64 108535933088016 to ptr), align 1
  br label %64

64:                                               ; preds = %61
  %65 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 4
  %66 = add i64 %65, 1
  store i64 %66, ptr inttoptr (i64 108535933091864 to ptr), align 4
  store i16 562, ptr inttoptr (i64 108535933088000 to ptr), align 2
  call void @skip_key_x_up()
  ret void
}

declare void @sync_timers()

declare void @bench_safepoint()

declare void @random_byte()

declare void @draw()

declare void @skip_key_x_up()
; --- LLVM trace after O2: ADDR212 ---
; ModuleID = 'CHIP-8'
source_filename = "CHIP-8"

define void @ADDR212() local_unnamed_addr {
  %1 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %2 = add i64 %1, 1
  store i64 %2, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 3, ptr inttoptr (i64 108535933088022 to ptr), align 2
  %3 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %4 = add i64 %3, 1
  store i64 %4, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 2, ptr inttoptr (i64 108535933088024 to ptr), align 8
  %5 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %6 = add i64 %5, 1
  store i64 %6, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 96, ptr inttoptr (i64 108535933088016 to ptr), align 16
  %7 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %8 = add i64 %7, 1
  store i64 %8, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %9 = load i8, ptr inttoptr (i64 108535933088016 to ptr), align 16
  tail call void @sync_timers()
  store i8 %9, ptr inttoptr (i64 108535933083883 to ptr), align 1
  br label %10

10:                                               ; preds = %45, %0
  %11 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %12 = add i64 %11, 1
  store i64 %12, ptr inttoptr (i64 108535933091864 to ptr), align 8
  tail call void @sync_timers()
  %13 = load i8, ptr inttoptr (i64 108535933083883 to ptr), align 1
  store i8 %13, ptr inttoptr (i64 108535933088016 to ptr), align 16
  %14 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %15 = add i64 %14, 1
  store i64 %15, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %16 = load i8, ptr inttoptr (i64 108535933088016 to ptr), align 16
  %17 = icmp eq i8 %16, 0
  br i1 %17, label %18, label %41

18:                                               ; preds = %10
  store i16 544, ptr inttoptr (i64 108535933088000 to ptr), align 256
  %19 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %20 = add i64 %19, 1
  store i64 %20, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 544, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @random_byte()
  %21 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %22 = add i64 %21, 1
  store i64 %22, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %23 = load i8, ptr inttoptr (i64 108535933088023 to ptr), align 1
  %24 = add i8 %23, 8
  store i8 %24, ptr inttoptr (i64 108535933088023 to ptr), align 1
  %25 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %26 = add i64 %25, 1
  store i64 %26, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 -1, ptr inttoptr (i64 108535933088025 to ptr), align 1
  %27 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %28 = add i64 %27, 1
  store i64 %28, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 752, ptr inttoptr (i64 108535933088002 to ptr), align 2
  %29 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %30 = add i64 %29, 1
  store i64 %30, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 552, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %31 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %32 = add i64 %31, 1
  store i64 %32, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 746, ptr inttoptr (i64 108535933088002 to ptr), align 2
  %33 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %34 = add i64 %33, 1
  store i64 %34, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 556, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %35 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %36 = add i64 %35, 1
  store i64 %36, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 558, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @draw()
  %37 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %38 = add i64 %37, 1
  store i64 %38, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i8 1, ptr inttoptr (i64 108535933088016 to ptr), align 16
  %39 = load i64, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %40 = add i64 %39, 1
  store i64 %40, ptr inttoptr (i64 108535933091864 to ptr), align 8
  store i16 562, ptr inttoptr (i64 108535933088000 to ptr), align 256
  tail call void @skip_key_x_up()
  br label %common.ret

41:                                               ; preds = %10
  %42 = add i64 %14, 2
  store i64 %42, ptr inttoptr (i64 108535933091864 to ptr), align 8
  %43 = load volatile i64, ptr inttoptr (i64 108535933079568 to ptr), align 16
  %.not = icmp slt i64 %42, %43
  br i1 %.not, label %45, label %44

44:                                               ; preds = %41
  tail call void @bench_safepoint()
  br label %45

45:                                               ; preds = %44, %41
  store i16 538, ptr inttoptr (i64 108535933088000 to ptr), align 256
  %46 = load volatile i8, ptr inttoptr (i64 108535933083816 to ptr), align 8
  %.not1 = icmp eq i8 %46, 0
  br i1 %.not1, label %10, label %common.ret

common.ret:                                       ; preds = %45, %18
  ret void
}

declare void @sync_timers() local_unnamed_addr

declare void @bench_safepoint() local_unnamed_addr

declare void @random_byte() local_unnamed_addr

declare void @draw() local_unnamed_addr

declare void @skip_key_x_up() local_unnamed_addr
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
elapsed = 0.013 s
rate = 0.08 Minsn/s
