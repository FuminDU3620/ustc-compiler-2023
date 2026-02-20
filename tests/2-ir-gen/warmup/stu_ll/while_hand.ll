; ModuleID = 'if.c'
source_filename = "if.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
    %1 = alloca i32, align 4
    %2 = alloca i32, align 4
    store i32 10, i32* %1, align 4
    store i32 0, i32* %2, align 4
    %3 = load i32, i32* %1, align 4
    %4 = load i32, i32* %2, align 4

8:
    %5 = icmp slt i32 %4, 0
    br i1 %5, label %6, label %7

6:

    %9 = add i32 %4, %1
    %10 = add i32 %3, %9
    

}