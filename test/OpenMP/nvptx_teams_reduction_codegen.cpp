// Test target codegen - host bc file has to be created first.
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple powerpc64le-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm-bc %s -o %t-ppc-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple nvptx64-unknown-unknown -fopenmp-targets=nvptx64-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-ppc-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-64
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple i386-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm-bc %s -o %t-x86-host.bc
// RUN: %clang_cc1 -verify -fopenmp -x c++ -triple nvptx-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-x86-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-32
// RUN: %clang_cc1 -verify -fopenmp -fexceptions -fcxx-exceptions -x c++ -triple nvptx-unknown-unknown -fopenmp-targets=nvptx-nvidia-cuda -emit-llvm %s -fopenmp-is-device -fopenmp-host-ir-file-path %t-x86-host.bc -o - | FileCheck %s --check-prefix CHECK --check-prefix CHECK-32
// expected-no-diagnostics
#ifndef HEADER
#define HEADER

// CHECK: [[MAP_TY:%.+]] = type { [16 x i8] }

// CHECK-DAG: [[KERNEL_PTR:@.+]] = internal addrspace(3) global i8* null
// CHECK-DAG: [[KERNEL_SHARED1:@.+]] = internal unnamed_addr constant i16 1
// CHECK-DAG: [[KERNEL_SHARED2:@.+]] = internal unnamed_addr constant i16 1
// CHECK-DAG: [[KERNEL_SHARED3:@.+]] = internal unnamed_addr constant i16 1
// CHECK-DAG: [[KERNEL_SIZE1:@.+]] = internal unnamed_addr constant i{{64|32}} {{16|8}}
// CHECK-DAG: [[KERNEL_SIZE2:@.+]] = internal unnamed_addr constant i{{64|32}} 16
// CHECK-DAG: [[KERNEL_SIZE3:@.+]] = internal unnamed_addr constant i{{64|32}} 8

// Check for the data transfer medium in shared memory to transfer the reduction list to the first warp.
// CHECK-DAG: [[TRANSFER_STORAGE:@.+]] = common addrspace([[SHARED_ADDRSPACE:[0-9]+]]) global [32 x i32]

// Check that the execution mode of 2 target regions is set to Non-SPMD and the 3rd is in SPMD.
// CHECK-DAG: {{@__omp_offloading_.+l37}}_exec_mode = weak constant i8 1
// CHECK-DAG: {{@__omp_offloading_.+l43}}_exec_mode = weak constant i8 1
// CHECK-DAG: {{@__omp_offloading_.+l50}}_exec_mode = weak constant i8 0

template<typename tx>
tx ftemplate(int n) {
  int a;
  short b;
  tx c;
  float d;
  double e;

  #pragma omp target
  #pragma omp teams reduction(+: e)
  {
    e += 5;
  }

  #pragma omp target
  #pragma omp teams reduction(^: c) reduction(*: d)
  {
    c ^= 2;
    d *= 33;
  }

  #pragma omp target
  #pragma omp teams reduction(|: a) reduction(max: b)
  #pragma omp parallel reduction(|: a) reduction(max: b)
  {
    a |= 1;
    b = 99 > b ? 99 : b;
  }

  return a+b+c+d+e;
}

int bar(int n){
  int a = 0;

  a += ftemplate<char>(n);

  return a;
}

  // CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+template.+l37}}_worker()

  // CHECK: define {{.*}}void [[T1:@__omp_offloading_.+template.+l37]](
  //
  // CHECK: {{call|invoke}} void [[T1]]_worker()
  //
  // CHECK: call void @__kmpc_kernel_init(
  //
  // CHECK: store double {{[0\.e\+]+}}, double* [[E:%.+]], align
  // CHECK: [[EV:%.+]] = load double, double* [[E]], align
  // CHECK: [[ADD:%.+]] = fadd double [[EV]], 5
  // CHECK: store double [[ADD]], double* [[E]], align
  // CHECK: [[PTR1:%.+]] = getelementptr inbounds [[RLT:.+]], [1 x i8*]* [[RL:%.+]], i[[SZ:32|64]] 0, i{{32|64}} 0
  // CHECK: [[E_CAST:%.+]] = bitcast double* [[E]] to i8*
  // CHECK: store i8* [[E_CAST]], i8** [[PTR1]], align
  // CHECK: [[ARG_RL:%.+]] = bitcast [[RLT]]* [[RL]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_teams_reduce_nowait(i32 {{.+}}, i32 1, i[[SZ]] {{4|8}}, i8* [[ARG_RL]], void (i8*, i16, i16, i16)* [[SHUFFLE_REDUCE_FN:@.+]], void (i8*, i32)* [[WARP_COPY_FN:@.+]], void (i8*, i8*, i32, i32)* [[SCRATCH_COPY_FN:@.+]], void (i8*, i8*, i32, i32, i32)* [[LOAD_REDUCE_FN:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[E_INV:%.+]] = load double, double* [[E_IN:%.+]], align
  // CHECK: [[EV:%.+]] = load double, double* [[E]], align
  // CHECK: [[ADD:%.+]] = fadd double [[E_INV]], [[EV]]
  // CHECK: store double [[ADD]], double* [[E_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: call void @__kmpc_kernel_deinit(

  //
  // Reduction function
  // CHECK: define internal void [[REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR_RHS_VOID:%.+]] = load i8*, i8** [[VAR_RHS_REF]],
  // CHECK: [[VAR_RHS:%.+]] = bitcast i8* [[VAR_RHS_VOID]] to double*
  //
  // CHECK: [[VAR_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR_LHS_VOID:%.+]] = load i8*, i8** [[VAR_LHS_REF]],
  // CHECK: [[VAR_LHS:%.+]] = bitcast i8* [[VAR_LHS_VOID]] to double*
  //
  // CHECK: [[VAR_LHS_VAL:%.+]] = load double, double* [[VAR_LHS]],
  // CHECK: [[VAR_RHS_VAL:%.+]] = load double, double* [[VAR_RHS]],
  // CHECK: [[RES:%.+]] = fadd double [[VAR_LHS_VAL]], [[VAR_RHS_VAL]]
  // CHECK: store double [[RES]], double* [[VAR_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[SHUFFLE_REDUCE_FN]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT:%.+]] = alloca double
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to double*
  //
  // CHECK: [[ELT_CAST:%.+]] = bitcast double* [[ELT]] to i64*
  // CHECK: [[REMOTE_ELT_CAST:%.+]] = bitcast double* [[REMOTE_ELT]] to i64*
  // CHECK: [[ELT_VAL:%.+]] = load i64, i64* [[ELT_CAST]], align
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT_VAL64:%.+]] = call i64 @__kmpc_shuffle_int64(i64 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i64 [[REMOTE_ELT_VAL64]], i64* [[REMOTE_ELT_CAST]], align
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = bitcast double* [[REMOTE_ELT]] to i8*
  // CHECK: store i8* [[REMOTE_ELT_VOID]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to double*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to double*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load double, double* [[REMOTE_ELT]], align
  // CHECK: store double [[REMOTE_ELT_VAL]], double* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[WARP_COPY_FN]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: store i32 0, i32* [[CNT_ADDR:%.+]],
  // CHECK: br label
  // CHECK: [[CNT:%.+]] = load i32, i32* [[CNT_ADDR]],
  // CHECK: [[DONE_COPY:%.+]] = icmp ult i32 [[CNT]], 2
  // CHECK: br i1 [[DONE_COPY]], label
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[BASE_ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = getelementptr i32, i32* [[BASE_ELT]], i32 [[CNT]]
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]],
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]],
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i{{32|64}} 0, i{{32|64}} 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT_BASE:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = getelementptr i32, i32* [[ELT_BASE]], i32 [[CNT]]
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]],
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]],
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: [[NEXT:%.+]] = add nsw i32 [[CNT]], 1
  // CHECK: store i32 [[NEXT]], i32* [[CNT_ADDR]],
  // CHECK: br label
  // CHECK: ret

  //
  // Copy to scratchpad function
  // CHECK: define internal void [[SCRATCH_COPY_FN]](i8*, i8*, i32, i32)
  // CHECK: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[SCRATCHPAD_PTR:%.+]] = load i8*, i8** {{.+}}, align
  // CHECK-64: [[TEAM32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[TEAM:%.+]] = sext i32 [[TEAM32]] to i64
  // CHECK-32: [[TEAM:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS:%.+]] = sext i32 [[NUM_TEAMS32]] to i64
  // CHECK-32: [[NUM_TEAMS:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SCRATCHPAD:%.+]] = ptrtoint i8* [[SCRATCHPAD_PTR]] to i[[SZ]]
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 8, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to double*
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to double*
  // CHECK: [[ELT_VAL:%.+]] = load double, double* [[ELT]], align
  // CHECK: store double [[ELT_VAL]], double* [[SCRATCHPAD_ELT_PTR]], align
  //
  // CHECK: ret

  //
  // Load and reduce function
  // CHECK: define internal void [[LOAD_REDUCE_FN]](i8*, i8*, i32, i32, i32)
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT:%.+]] = alloca double
  // CHECK: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[SCRATCHPAD_PTR:%.+]] = load i8*, i8** {{.+}}, align
  // CHECK-64: [[TEAM32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[TEAM:%.+]] = sext i32 [[TEAM32]] to i64
  // CHECK-32: [[TEAM:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS:%.+]] = sext i32 [[NUM_TEAMS32]] to i64
  // CHECK-32: [[NUM_TEAMS:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SHOULD_REDUCE:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SCRATCHPAD:%.+]] = ptrtoint i8* [[SCRATCHPAD_PTR]] to i[[SZ]]
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 8, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*

  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to double*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load double, double* [[SCRATCHPAD_ELT_PTR]], align
  // CHECK: store double [[REMOTE_ELT_VAL]], double* [[REMOTE_ELT]], align
  // CHECK: [[REMOTE_ELT_PTR:%.+]] = bitcast double* [[REMOTE_ELT]] to i8*
  // CHECK: store i8* [[REMOTE_ELT_PTR]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[REDUCE:%.+]] = icmp ne i32 [[SHOULD_REDUCE]], 0
  // CHECK: br i1 [[REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // Copy element from remote reduce list
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to double*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to double*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load double, double* [[REMOTE_ELT]], align
  // CHECK: store double [[REMOTE_ELT_VAL]], double* [[ELT]], align
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // CHECK: ret

  // CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+template.+l43}}_worker()

  // CHECK: define {{.*}}void [[T2:@__omp_offloading_.+template.+l43]](
  //
  // CHECK: {{call|invoke}} void [[T2]]_worker()
  //
  // CHECK: call void @__kmpc_kernel_init(
  //
  // CHECK: store float {{1\.[0e\+]+}}, float* [[D:%.+]], align
  // CHECK: [[C_VAL:%.+]] = load i8, i8* [[C:%.+]], align
  // CHECK: [[CONV:%.+]] = sext i8 [[C_VAL]] to i32
  // CHECK: [[XOR:%.+]] = xor i32 [[CONV]], 2
  // CHECK: [[TRUNC:%.+]] = trunc i32 [[XOR]] to i8
  // CHECK: store i8 [[TRUNC]], i8* [[C]], align
  // CHECK: [[DV:%.+]] = load float, float* [[D]], align
  // CHECK: [[MUL:%.+]] = fmul float [[DV]], {{[0-9e\.\+]+}}
  // CHECK: store float [[MUL]], float* [[D]], align
  // CHECK: [[PTR1:%.+]] = getelementptr inbounds [[RLT:.+]], [2 x i8*]* [[RL:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: store i8* [[C]], i8** [[PTR1]], align
  // CHECK: [[PTR2:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RL]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[D_CAST:%.+]] = bitcast float* [[D]] to i8*
  // CHECK: store i8* [[D_CAST]], i8** [[PTR2]], align
  // CHECK: [[ARG_RL:%.+]] = bitcast [[RLT]]* [[RL]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_teams_reduce_nowait(i32 {{.+}}, i32 2, i[[SZ]] {{8|16}}, i8* [[ARG_RL]], void (i8*, i16, i16, i16)* [[SHUFFLE_REDUCE_FN:@.+]], void (i8*, i32)* [[WARP_COPY_FN:@.+]], void (i8*, i8*, i32, i32)* [[SCRATCH_COPY_FN:@.+]], void (i8*, i8*, i32, i32, i32)* [[LOAD_REDUCE_FN:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[C_INV8:%.+]] = load i8, i8* [[C_IN:%.+]], align
  // CHECK: [[C_INV:%.+]] = sext i8 [[C_INV8]] to i32
  // CHECK: [[CV8:%.+]] = load i8, i8* [[C]], align
  // CHECK: [[CV:%.+]] = sext i8 [[CV8]] to i32
  // CHECK: [[XOR:%.+]] = xor i32 [[C_INV]], [[CV]]
  // CHECK: [[TRUNC:%.+]] = trunc i32 [[XOR]] to i8
  // CHECK: store i8 [[TRUNC]], i8* [[C_IN]], align
  // CHECK: [[D_INV:%.+]] = load float, float* [[D_IN:%.+]], align
  // CHECK: [[DV:%.+]] = load float, float* [[D]], align
  // CHECK: [[MUL:%.+]] = fmul float [[D_INV]], [[DV]]
  // CHECK: store float [[MUL]], float* [[D_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: call void @__kmpc_kernel_deinit(

  //
  // Reduction function
  // CHECK: define internal void [[REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR1_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_RHS:%.+]] = load i8*, i8** [[VAR1_RHS_REF]],
  //
  // CHECK: [[VAR1_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_LHS:%.+]] = load i8*, i8** [[VAR1_LHS_REF]],
  //
  // CHECK: [[VAR2_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_RHS_VOID:%.+]] = load i8*, i8** [[VAR2_RHS_REF]],
  // CHECK: [[VAR2_RHS:%.+]] = bitcast i8* [[VAR2_RHS_VOID]] to float*
  //
  // CHECK: [[VAR2_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_LHS_VOID:%.+]] = load i8*, i8** [[VAR2_LHS_REF]],
  // CHECK: [[VAR2_LHS:%.+]] = bitcast i8* [[VAR2_LHS_VOID]] to float*
  //
  // CHECK: [[VAR1_LHS_VAL8:%.+]] = load i8, i8* [[VAR1_LHS]],
  // CHECK: [[VAR1_LHS_VAL:%.+]] = sext i8 [[VAR1_LHS_VAL8]] to i32
  // CHECK: [[VAR1_RHS_VAL8:%.+]] = load i8, i8* [[VAR1_RHS]],
  // CHECK: [[VAR1_RHS_VAL:%.+]] = sext i8 [[VAR1_RHS_VAL8]] to i32
  // CHECK: [[XOR:%.+]] = xor i32 [[VAR1_LHS_VAL]], [[VAR1_RHS_VAL]]
  // CHECK: [[RES:%.+]] = trunc i32 [[XOR]] to i8
  // CHECK: store i8 [[RES]], i8* [[VAR1_LHS]],
  //
  // CHECK: [[VAR2_LHS_VAL:%.+]] = load float, float* [[VAR2_LHS]],
  // CHECK: [[VAR2_RHS_VAL:%.+]] = load float, float* [[VAR2_RHS]],
  // CHECK: [[RES:%.+]] = fmul float [[VAR2_LHS_VAL]], [[VAR2_RHS_VAL]]
  // CHECK: store float [[RES]], float* [[VAR2_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[SHUFFLE_REDUCE_FN]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i8
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca float
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VAL:%.+]] = load i8, i8* [[ELT_VOID]], align
  //
  // CHECK: [[ELT_CAST:%.+]] = sext i8 [[ELT_VAL]] to i32
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT1_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_CAST]], i16 [[LANEOFFSET]], i16 [[WS]])
  // CHECK: [[REMOTE_ELT1_VAL:%.+]] = trunc i32 [[REMOTE_ELT1_VAL32]] to i8
  //
  // CHECK: store i8 [[REMOTE_ELT1_VAL]], i8* [[REMOTE_ELT1]], align
  // CHECK: store i8* [[REMOTE_ELT1]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to float*
  //
  // CHECK: [[ELT_CAST:%.+]] = bitcast float* [[ELT]] to i32*
  // CHECK: [[REMOTE_ELT2_CAST:%.+]] = bitcast float* [[REMOTE_ELT2]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT_CAST]], align
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT2_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i32 [[REMOTE_ELT2_VAL32]], i32* [[REMOTE_ELT2_CAST]], align
  // CHECK: [[REMOTE_ELT2C:%.+]] = bitcast float* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT2C]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i8, i8* [[REMOTE_ELT_VOID]], align
  // CHECK: store i8 [[REMOTE_ELT_VAL]], i8* [[ELT_VOID]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to float*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to float*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load float, float* [[REMOTE_ELT]], align
  // CHECK: store float [[REMOTE_ELT_VAL]], float* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[WARP_COPY_FN]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i8 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_VAL:%.+]] = load i8, i8* [[ELT_VOID]], align
  // CHECK: store volatile i8 [[ELT_VAL]], i8 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i8 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i8, i8 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i8 [[MEDIUM_ELT_VAL]], i8* [[ELT_VOID]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: ret

  //
  // Copy to scratchpad function
  // CHECK: define internal void [[SCRATCH_COPY_FN]](i8*, i8*, i32, i32)
  // CHECK: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[SCRATCHPAD_PTR:%.+]] = load i8*, i8** {{.+}}, align
  // CHECK-64: [[TEAM32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[TEAM:%.+]] = sext i32 [[TEAM32]] to i64
  // CHECK-32: [[TEAM:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS:%.+]] = sext i32 [[NUM_TEAMS32]] to i64
  // CHECK-32: [[NUM_TEAMS:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SCRATCHPAD:%.+]] = ptrtoint i8* [[SCRATCHPAD_PTR]] to i[[SZ]]
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 1, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*
  // CHECK: [[ELT_VAL:%.+]] = load i8, i8* [[ELT_VOID]], align
  // CHECK: store i8 [[ELT_VAL]], i8* [[SCRATCHPAD_ELT_PTR]], align
  //
  // CHECK: [[OF:%.+]] = mul nuw i[[SZ]] [[NUM_TEAMS]], 1
  // CHECK: [[POS1:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[OF]]
  // CHECK: [[POS2:%.+]] = sub nuw i[[SZ]] [[POS1]], 1
  // CHECK: [[POS3:%.+]] = udiv i[[SZ]] [[POS2]], 128
  // CHECK: [[POS4:%.+]] = add nuw i[[SZ]] [[POS3]], 1
  // CHECK: [[SCRATCHPAD_NEXT:%.+]] = mul nuw i[[SZ]] [[POS4]], 128
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 4, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD_NEXT]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to float*
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to float*
  // CHECK: [[ELT_VAL:%.+]] = load float, float* [[ELT]], align
  // CHECK: store float [[ELT_VAL]], float* [[SCRATCHPAD_ELT_PTR]], align
  //
  // CHECK: ret

  //
  // Load and reduce function
  // CHECK: define internal void [[LOAD_REDUCE_FN]](i8*, i8*, i32, i32, i32)
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i8
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca float
  // CHECK: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[SCRATCHPAD_PTR:%.+]] = load i8*, i8** {{.+}}, align
  // CHECK-64: [[TEAM32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[TEAM:%.+]] = sext i32 [[TEAM32]] to i64
  // CHECK-32: [[TEAM:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS:%.+]] = sext i32 [[NUM_TEAMS32]] to i64
  // CHECK-32: [[NUM_TEAMS:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SHOULD_REDUCE:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SCRATCHPAD:%.+]] = ptrtoint i8* [[SCRATCHPAD_PTR]] to i[[SZ]]
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 1, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*

  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i8, i8* [[SCRATCHPAD_ELT_PTR_VOID]], align
  // CHECK: store i8 [[REMOTE_ELT_VAL]], i8* [[REMOTE_ELT1]], align
  // CHECK: store i8* [[REMOTE_ELT1]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[OF:%.+]] = mul nuw i[[SZ]] [[NUM_TEAMS]], 1
  // CHECK: [[POS1:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[OF]]
  // CHECK: [[POS2:%.+]] = sub nuw i[[SZ]] [[POS1]], 1
  // CHECK: [[POS3:%.+]] = udiv i[[SZ]] [[POS2]], 128
  // CHECK: [[POS4:%.+]] = add nuw i[[SZ]] [[POS3]], 1
  // CHECK: [[SCRATCHPAD_NEXT:%.+]] = mul nuw i[[SZ]] [[POS4]], 128
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 4, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD_NEXT]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*

  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to float*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load float, float* [[SCRATCHPAD_ELT_PTR]], align
  // CHECK: store float [[REMOTE_ELT_VAL]], float* [[REMOTE_ELT2]], align
  // CHECK: [[REMOTE_ELT_PTR:%.+]] = bitcast float* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT_PTR]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[REDUCE:%.+]] = icmp ne i32 [[SHOULD_REDUCE]], 0
  // CHECK: br i1 [[REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // Copy element from remote reduce list
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i8, i8* [[REMOTE_ELT_VOID]], align
  // CHECK: store i8 [[REMOTE_ELT_VAL]], i8* [[ELT_VOID]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to float*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to float*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load float, float* [[REMOTE_ELT]], align
  // CHECK: store float [[REMOTE_ELT_VAL]], float* [[ELT]], align
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // CHECK: ret

  // CHECK-LABEL: define {{.*}}void {{@__omp_offloading_.+template.+l50}}(
  //
  // CHECK: call void @__kmpc_spmd_kernel_init(
  // CHECK: call void @__kmpc_data_sharing_init_stack_spmd()
  // CHECK: call void @__kmpc_get_team_static_memory(i8* addrspacecast (i8 addrspace(3)* getelementptr inbounds ([[MEM_TY:%.+]], %{{.+}} addrspace(3)* [[KERNEL_RD:@.+]], i32 0, i32 0, i32 0) to i8*), i{{64|32}} {{8|16}}, i16 1, i8** addrspacecast (i8* addrspace(3)* [[KERNEL_PTR:@.+]] to i8**))
  // CHECK: [[PTR:%.+]] = load i8*, i8* addrspace(3)* [[KERNEL_PTR]],
  // CHECK: [[GLOBAL_REC:%.+]] = bitcast i8* [[PTR]] to [[GLOB_REC_TY:%.+]]*
  // CHECK-DAG: [[A_ADDR:%.+]] = getelementptr inbounds [[GLOB_REC_TY]], [[GLOB_REC_TY]]* [[GLOBAL_REC]], i32 0, i32 0
  // CHECK-DAG: [[B_ADDR:%.+]] = getelementptr inbounds [[GLOB_REC_TY]], [[GLOB_REC_TY]]* [[GLOBAL_REC]], i32 0, i32 1
  // CHECK: store i32 0, i32* [[A_ADDR]],
  // CHECK: store i16 -32768, i16* [[B_ADDR]],
  // CHECK: call void [[OUTLINED:@.+]](i32* {{.+}}, i32* {{.+}}, i32* [[A_ADDR]], i16* [[B_ADDR]])
  // CHECK: [[PTR1:%.+]] = getelementptr inbounds [[RLT:.+]], [2 x i8*]* [[RL:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[A_CAST:%.+]] = bitcast i32* [[A_ADDR]] to i8*
  // CHECK: store i8* [[A_CAST]], i8** [[PTR1]], align
  // CHECK: [[PTR2:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RL]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[B_CAST:%.+]] = bitcast i16* [[B_ADDR]] to i8*
  // CHECK: store i8* [[B_CAST]], i8** [[PTR2]], align
  // CHECK: [[ARG_RL:%.+]] = bitcast [[RLT]]* [[RL]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_teams_reduce_nowait(i32 {{.+}}, i32 2, i[[SZ]] {{8|16}}, i8* [[ARG_RL]], void (i8*, i16, i16, i16)* [[SHUFFLE_REDUCE_FN:@.+]], void (i8*, i32)* [[WARP_COPY_FN:@.+]], void (i8*, i8*, i32, i32)* [[SCRATCH_COPY_FN:@.+]], void (i8*, i8*, i32, i32, i32)* [[LOAD_REDUCE_FN:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[A_INV:%.+]] = load i32, i32* [[A_IN:%.+]], align
  // CHECK: [[AV:%.+]] = load i32, i32* [[A_ADDR]], align
  // CHECK: [[OR:%.+]] = or i32 [[A_INV]], [[AV]]
  // CHECK: store i32 [[OR]], i32* [[A_IN]], align
  // CHECK: [[B_INV16:%.+]] = load i16, i16* [[B_IN:%.+]], align
  // CHECK: [[B_INV:%.+]] = sext i16 [[B_INV16]] to i32
  // CHECK: [[BV16:%.+]] = load i16, i16* [[B_ADDR]], align
  // CHECK: [[BV:%.+]] = sext i16 [[BV16]] to i32
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[B_INV]], [[BV]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[B_IN]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[B_ADDR]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[B_MAX:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[B_MAX]], i16* [[B_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // call void @__kmpc_restore_team_static_memory(i16 1)
  // CHECK: call void @__kmpc_spmd_kernel_deinit(

  // CHECK: define internal void [[OUTLINED]](i32* noalias %{{.+}}, i32* noalias %{{.+}}, i32* dereferenceable{{.+}}, i16* dereferenceable{{.+}})
  //
  // CHECK: store i32 0, i32* [[A:%.+]], align
  // CHECK: store i16 -32768, i16* [[B:%.+]], align
  // CHECK: [[A_VAL:%.+]] = load i32, i32* [[A:%.+]], align
  // CHECK: [[OR:%.+]] = or i32 [[A_VAL]], 1
  // CHECK: store i32 [[OR]], i32* [[A]], align
  // CHECK: [[BV16:%.+]] = load i16, i16* [[B]], align
  // CHECK: [[BV:%.+]] = sext i16 [[BV16]] to i32
  // CHECK: [[CMP:%.+]] = icmp sgt i32 99, [[BV]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[BV:%.+]] = load i16, i16* [[B]], align
  // CHECK: [[MAX:%.+]] = sext i16 [[BV]] to i32
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[B_LVALUE:%.+]] = phi i32 [ 99, %[[DO_MAX]] ], [ [[MAX]], %[[MAX_ELSE]] ]
  // CHECK: [[TRUNC:%.+]] = trunc i32 [[B_LVALUE]] to i16
  // CHECK: store i16 [[TRUNC]], i16* [[B]], align
  // CHECK: [[PTR1:%.+]] = getelementptr inbounds [[RLT:.+]], [2 x i8*]* [[RL:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[A_CAST:%.+]] = bitcast i32* [[A]] to i8*
  // CHECK: store i8* [[A_CAST]], i8** [[PTR1]], align
  // CHECK: [[PTR2:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RL]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[B_CAST:%.+]] = bitcast i16* [[B]] to i8*
  // CHECK: store i8* [[B_CAST]], i8** [[PTR2]], align
  // CHECK: [[ARG_RL:%.+]] = bitcast [[RLT]]* [[RL]] to i8*
  // CHECK: [[RET:%.+]] = call i32 @__kmpc_nvptx_parallel_reduce_nowait(i32 {{.+}}, i32 2, i[[SZ]] {{8|16}}, i8* [[ARG_RL]], void (i8*, i16, i16, i16)* [[PAR_SHUFFLE_REDUCE_FN:@.+]], void (i8*, i32)* [[PAR_WARP_COPY_FN:@.+]])
  // CHECK: [[COND:%.+]] = icmp eq i32 [[RET]], 1
  // CHECK: br i1 [[COND]], label {{%?}}[[IFLABEL:.+]], label {{%?}}[[EXIT:.+]]
  //
  // CHECK: [[IFLABEL]]
  // CHECK: [[A_INV:%.+]] = load i32, i32* [[A_IN:%.+]], align
  // CHECK: [[AV:%.+]] = load i32, i32* [[A]], align
  // CHECK: [[OR:%.+]] = or i32 [[A_INV]], [[AV]]
  // CHECK: store i32 [[OR]], i32* [[A_IN]], align
  // CHECK: [[B_INV16:%.+]] = load i16, i16* [[B_IN:%.+]], align
  // CHECK: [[B_INV:%.+]] = sext i16 [[B_INV16]] to i32
  // CHECK: [[BV16:%.+]] = load i16, i16* [[B]], align
  // CHECK: [[BV:%.+]] = sext i16 [[BV16]] to i32
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[B_INV]], [[BV]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[B_IN]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[B]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[B_MAX:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[B_MAX]], i16* [[B_IN]], align
  // CHECK: call void @__kmpc_nvptx_end_reduce_nowait(
  // CHECK: br label %[[EXIT]]
  //
  // CHECK: [[EXIT]]
  // CHECK: ret void

  //
  // Reduction function
  // CHECK: define internal void [[PAR_REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR1_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_RHS_VOID:%.+]] = load i8*, i8** [[VAR1_RHS_REF]],
  // CHECK: [[VAR1_RHS:%.+]] = bitcast i8* [[VAR1_RHS_VOID]] to i32*
  //
  // CHECK: [[VAR1_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_LHS_VOID:%.+]] = load i8*, i8** [[VAR1_LHS_REF]],
  // CHECK: [[VAR1_LHS:%.+]] = bitcast i8* [[VAR1_LHS_VOID]] to i32*
  //
  // CHECK: [[VAR2_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_RHS_VOID:%.+]] = load i8*, i8** [[VAR2_RHS_REF]],
  // CHECK: [[VAR2_RHS:%.+]] = bitcast i8* [[VAR2_RHS_VOID]] to i16*
  //
  // CHECK: [[VAR2_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_LHS_VOID:%.+]] = load i8*, i8** [[VAR2_LHS_REF]],
  // CHECK: [[VAR2_LHS:%.+]] = bitcast i8* [[VAR2_LHS_VOID]] to i16*
  //
  // CHECK: [[VAR1_LHS_VAL:%.+]] = load i32, i32* [[VAR1_LHS]],
  // CHECK: [[VAR1_RHS_VAL:%.+]] = load i32, i32* [[VAR1_RHS]],
  // CHECK: [[OR:%.+]] = or i32 [[VAR1_LHS_VAL]], [[VAR1_RHS_VAL]]
  // CHECK: store i32 [[OR]], i32* [[VAR1_LHS]],
  //
  // CHECK: [[VAR2_LHS_VAL16:%.+]] = load i16, i16* [[VAR2_LHS]],
  // CHECK: [[VAR2_LHS_VAL:%.+]] = sext i16 [[VAR2_LHS_VAL16]] to i32
  // CHECK: [[VAR2_RHS_VAL16:%.+]] = load i16, i16* [[VAR2_RHS]],
  // CHECK: [[VAR2_RHS_VAL:%.+]] = sext i16 [[VAR2_RHS_VAL16]] to i32
  //
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[VAR2_LHS_VAL]], [[VAR2_RHS_VAL]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[VAR2_LHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[VAR2_RHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[MAXV:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[MAXV]], i16* [[VAR2_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[PAR_SHUFFLE_REDUCE_FN]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i32
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca i16
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  //
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT1_VAL:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i32 [[REMOTE_ELT1_VAL]], i32* [[REMOTE_ELT1]], align
  // CHECK: [[REMOTE_ELT1C:%.+]] = bitcast i32* [[REMOTE_ELT1]] to i8*
  // CHECK: store i8* [[REMOTE_ELT1C]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  //
  // CHECK: [[ELT_CAST:%.+]] = sext i16 [[ELT_VAL]] to i32
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT2_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_CAST]], i16 [[LANEOFFSET]], i16 [[WS]])
  // CHECK: [[REMOTE_ELT2_VAL:%.+]] = trunc i32 [[REMOTE_ELT2_VAL32]] to i16
  //
  // CHECK: store i16 [[REMOTE_ELT2_VAL]], i16* [[REMOTE_ELT2]], align
  // CHECK: [[REMOTE_ELT2C:%.+]] = bitcast i16* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT2C]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[PAR_REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i32, i32* [[REMOTE_ELT]], align
  // CHECK: store i32 [[REMOTE_ELT_VAL]], i32* [[ELT]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i16*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i16, i16* [[REMOTE_ELT]], align
  // CHECK: store i16 [[REMOTE_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[PAR_WARP_COPY_FN]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  //
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  // CHECK: store volatile i16 [[ELT_VAL]], i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i16, i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i16 [[MEDIUM_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: ret

  //
  // Reduction function
  // CHECK: define internal void [[REDUCTION_FUNC:@.+]](i8*, i8*)
  // CHECK: [[VAR1_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_RHS_VOID:%.+]] = load i8*, i8** [[VAR1_RHS_REF]],
  // CHECK: [[VAR1_RHS:%.+]] = bitcast i8* [[VAR1_RHS_VOID]] to i32*
  //
  // CHECK: [[VAR1_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[VAR1_LHS_VOID:%.+]] = load i8*, i8** [[VAR1_LHS_REF]],
  // CHECK: [[VAR1_LHS:%.+]] = bitcast i8* [[VAR1_LHS_VOID]] to i32*
  //
  // CHECK: [[VAR2_RHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_RHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_RHS_VOID:%.+]] = load i8*, i8** [[VAR2_RHS_REF]],
  // CHECK: [[VAR2_RHS:%.+]] = bitcast i8* [[VAR2_RHS_VOID]] to i16*
  //
  // CHECK: [[VAR2_LHS_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST_LHS]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[VAR2_LHS_VOID:%.+]] = load i8*, i8** [[VAR2_LHS_REF]],
  // CHECK: [[VAR2_LHS:%.+]] = bitcast i8* [[VAR2_LHS_VOID]] to i16*
  //
  // CHECK: [[VAR1_LHS_VAL:%.+]] = load i32, i32* [[VAR1_LHS]],
  // CHECK: [[VAR1_RHS_VAL:%.+]] = load i32, i32* [[VAR1_RHS]],
  // CHECK: [[OR:%.+]] = or i32 [[VAR1_LHS_VAL]], [[VAR1_RHS_VAL]]
  // CHECK: store i32 [[OR]], i32* [[VAR1_LHS]],
  //
  // CHECK: [[VAR2_LHS_VAL16:%.+]] = load i16, i16* [[VAR2_LHS]],
  // CHECK: [[VAR2_LHS_VAL:%.+]] = sext i16 [[VAR2_LHS_VAL16]] to i32
  // CHECK: [[VAR2_RHS_VAL16:%.+]] = load i16, i16* [[VAR2_RHS]],
  // CHECK: [[VAR2_RHS_VAL:%.+]] = sext i16 [[VAR2_RHS_VAL16]] to i32
  //
  // CHECK: [[CMP:%.+]] = icmp sgt i32 [[VAR2_LHS_VAL]], [[VAR2_RHS_VAL]]
  // CHECK: br i1 [[CMP]], label {{%?}}[[DO_MAX:.+]], label {{%?}}[[MAX_ELSE:.+]]
  //
  // CHECK: [[DO_MAX]]
  // CHECK: [[MAX1:%.+]] = load i16, i16* [[VAR2_LHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT:.+]]
  //
  // CHECK: [[MAX_ELSE]]
  // CHECK: [[MAX2:%.+]] = load i16, i16* [[VAR2_RHS]], align
  // CHECK: br label {{%?}}[[MAX_CONT]]
  //
  // CHECK: [[MAX_CONT]]
  // CHECK: [[MAXV:%.+]] = phi i16 [ [[MAX1]], %[[DO_MAX]] ], [ [[MAX2]], %[[MAX_ELSE]] ]
  // CHECK: store i16 [[MAXV]], i16* [[VAR2_LHS]],
  // CHECK: ret void

  //
  // Shuffle and reduce function
  // CHECK: define internal void [[SHUFFLE_REDUCE_FN]](i8*, i16 {{.*}}, i16 {{.*}}, i16 {{.*}})
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i32
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca i16
  //
  // CHECK: [[LANEID:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[LANEOFFSET:%.+]] = load i16, i16* {{.+}}, align
  // CHECK: [[ALGVER:%.+]] = load i16, i16* {{.+}}, align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  //
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT1_VAL:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_VAL]], i16 [[LANEOFFSET]], i16 [[WS]])
  //
  // CHECK: store i32 [[REMOTE_ELT1_VAL]], i32* [[REMOTE_ELT1]], align
  // CHECK: [[REMOTE_ELT1C:%.+]] = bitcast i32* [[REMOTE_ELT1]] to i8*
  // CHECK: store i8* [[REMOTE_ELT1C]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  //
  // CHECK: [[ELT_CAST:%.+]] = sext i16 [[ELT_VAL]] to i32
  // CHECK: [[WS32:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[WS:%.+]] = trunc i32 [[WS32]] to i16
  // CHECK: [[REMOTE_ELT2_VAL32:%.+]] = call i32 @__kmpc_shuffle_int32(i32 [[ELT_CAST]], i16 [[LANEOFFSET]], i16 [[WS]])
  // CHECK: [[REMOTE_ELT2_VAL:%.+]] = trunc i32 [[REMOTE_ELT2_VAL32]] to i16
  //
  // CHECK: store i16 [[REMOTE_ELT2_VAL]], i16* [[REMOTE_ELT2]], align
  // CHECK: [[REMOTE_ELT2C:%.+]] = bitcast i16* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT2C]], i8** [[REMOTE_ELT_REF]], align
  //
  // Condition to reduce
  // CHECK: [[CONDALG0:%.+]] = icmp eq i16 [[ALGVER]], 0
  //
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp ult i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[CONDALG1:%.+]] = and i1 [[COND1]], [[COND2]]
  //
  // CHECK: [[COND3:%.+]] = icmp eq i16 [[ALGVER]], 2
  // CHECK: [[COND4:%.+]] = and i16 [[LANEID]], 1
  // CHECK: [[COND5:%.+]] = icmp eq i16 [[COND4]], 0
  // CHECK: [[COND6:%.+]] = and i1 [[COND3]], [[COND5]]
  // CHECK: [[COND7:%.+]] = icmp sgt i16 [[LANEOFFSET]], 0
  // CHECK: [[CONDALG2:%.+]] = and i1 [[COND6]], [[COND7]]
  //
  // CHECK: [[COND8:%.+]] = or i1 [[CONDALG0]], [[CONDALG1]]
  // CHECK: [[SHOULD_REDUCE:%.+]] = or i1 [[COND8]], [[CONDALG2]]
  // CHECK: br i1 [[SHOULD_REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // Now check if we should just copy over the remote reduction list
  // CHECK: [[COND1:%.+]] = icmp eq i16 [[ALGVER]], 1
  // CHECK: [[COND2:%.+]] = icmp uge i16 [[LANEID]], [[LANEOFFSET]]
  // CHECK: [[SHOULD_COPY:%.+]] = and i1 [[COND1]], [[COND2]]
  // CHECK: br i1 [[SHOULD_COPY]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // CHECK: [[DO_COPY]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i32, i32* [[REMOTE_ELT]], align
  // CHECK: store i32 [[REMOTE_ELT_VAL]], i32* [[ELT]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i16*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i16, i16* [[REMOTE_ELT]], align
  // CHECK: store i16 [[REMOTE_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // CHECK: [[COPY_CONT]]
  // CHECK: void

  //
  // Inter warp copy function
  // CHECK: define internal void [[WARP_COPY_FN]](i8*, i32)
  // CHECK-DAG: [[LANEID:%.+]] = and i32 {{.+}}, 31
  // CHECK-DAG: [[WARPID:%.+]] = ashr i32 {{.+}}, 5
  // CHECK-DAG: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  //
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store volatile i32 [[ELT_VAL]], i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i32, i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i32 [[MEDIUM_ELT_VAL]], i32* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: [[IS_WARP_MASTER:%.+]] = icmp eq i32 [[LANEID]], 0
  // CHECK: br i1 [[IS_WARP_MASTER]], label {{%?}}[[DO_COPY:.+]], label {{%?}}[[COPY_ELSE:.+]]
  //
  // [[DO_COPY]]
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  //
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[WARPID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  // CHECK: store volatile i16 [[ELT_VAL]], i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: br label {{%?}}[[COPY_CONT:.+]]
  //
  // CHECK: [[COPY_ELSE]]
  // CHECK: br label {{%?}}[[COPY_CONT]]
  //
  // Barrier after copy to shared memory storage medium.
  // CHECK: [[COPY_CONT]]
  // CHECK: [[WS:%.+]] = call i32 @llvm.nvvm.read.ptx.sreg.warpsize()
  // CHECK: [[ACTIVE_THREADS:%.+]] = mul nsw i32 [[ACTIVE_WARPS:%.+]], [[WS]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  //
  // Read into warp 0.
  // CHECK: [[IS_W0_ACTIVE_THREAD:%.+]] = icmp ult i32 [[TID:%.+]], [[ACTIVE_WARPS]]
  // CHECK: br i1 [[IS_W0_ACTIVE_THREAD]], label {{%?}}[[DO_READ:.+]], label {{%?}}[[READ_ELSE:.+]]
  //
  // CHECK: [[DO_READ]]
  // CHECK: [[MEDIUM_ELT32:%.+]] = getelementptr inbounds [32 x i32], [32 x i32] addrspace([[SHARED_ADDRSPACE]])* [[TRANSFER_STORAGE]], i64 0, i32 [[TID]]
  // CHECK: [[MEDIUM_ELT:%.+]] = bitcast i32 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT32]] to i16 addrspace([[SHARED_ADDRSPACE]])*
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[MEDIUM_ELT_VAL:%.+]] = load volatile i16, i16 addrspace([[SHARED_ADDRSPACE]])* [[MEDIUM_ELT]], align
  // CHECK: store i16 [[MEDIUM_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[READ_CONT:.+]]
  //
  // CHECK: [[READ_ELSE]]
  // CHECK: br label {{%?}}[[READ_CONT]]
  //
  // CHECK: [[READ_CONT]]
  // CHECK: call void @llvm.nvvm.barrier(i32 1, i32 [[ACTIVE_THREADS]])
  // CHECK: ret

  //
  // Copy to scratchpad function
  // CHECK: define internal void [[SCRATCH_COPY_FN]](i8*, i8*, i32, i32)
  // CHECK: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[SCRATCHPAD_PTR:%.+]] = load i8*, i8** {{.+}}, align
  // CHECK-64: [[TEAM32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[TEAM:%.+]] = sext i32 [[TEAM32]] to i64
  // CHECK-32: [[TEAM:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS:%.+]] = sext i32 [[NUM_TEAMS32]] to i64
  // CHECK-32: [[NUM_TEAMS:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SCRATCHPAD:%.+]] = ptrtoint i8* [[SCRATCHPAD_PTR]] to i[[SZ]]
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 4, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to i32*
  // CHECK: [[ELT_VAL:%.+]] = load i32, i32* [[ELT]], align
  // CHECK: store i32 [[ELT_VAL]], i32* [[SCRATCHPAD_ELT_PTR]], align
  //
  // CHECK: [[OF:%.+]] = mul nuw i[[SZ]] [[NUM_TEAMS]], 4
  // CHECK: [[POS1:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[OF]]
  // CHECK: [[POS2:%.+]] = sub nuw i[[SZ]] [[POS1]], 1
  // CHECK: [[POS3:%.+]] = udiv i[[SZ]] [[POS2]], 128
  // CHECK: [[POS4:%.+]] = add nuw i[[SZ]] [[POS3]], 1
  // CHECK: [[SCRATCHPAD_NEXT:%.+]] = mul nuw i[[SZ]] [[POS4]], 128
  //
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 2, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD_NEXT]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to i16*
  // CHECK: [[ELT_VAL:%.+]] = load i16, i16* [[ELT]], align
  // CHECK: store i16 [[ELT_VAL]], i16* [[SCRATCHPAD_ELT_PTR]], align
  //
  // CHECK: ret

  //
  // Load and reduce function
  // CHECK: define internal void [[LOAD_REDUCE_FN]](i8*, i8*, i32, i32, i32)
  // CHECK: [[REMOTE_RED_LIST:%.+]] = alloca [[RLT]], align
  // CHECK: [[REMOTE_ELT1:%.+]] = alloca i32
  // CHECK: [[REMOTE_ELT2:%.+]] = alloca i16
  // CHECK: [[RED_LIST:%.+]] = bitcast i8* {{.+}} to [[RLT]]*
  // CHECK: [[SCRATCHPAD_PTR:%.+]] = load i8*, i8** {{.+}}, align
  // CHECK-64: [[TEAM32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[TEAM:%.+]] = sext i32 [[TEAM32]] to i64
  // CHECK-32: [[TEAM:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS32:%.+]] = load i32, i32* {{.+}}, align
  // CHECK-64: [[NUM_TEAMS:%.+]] = sext i32 [[NUM_TEAMS32]] to i64
  // CHECK-32: [[NUM_TEAMS:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SHOULD_REDUCE:%.+]] = load i32, i32* {{.+}}, align
  // CHECK: [[SCRATCHPAD:%.+]] = ptrtoint i8* [[SCRATCHPAD_PTR]] to i[[SZ]]
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 4, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*

  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to i32*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i32, i32* [[SCRATCHPAD_ELT_PTR]], align
  // CHECK: store i32 [[REMOTE_ELT_VAL]], i32* [[REMOTE_ELT1]], align
  // CHECK: [[REMOTE_ELT1_PTR:%.+]] = bitcast i32* [[REMOTE_ELT1]] to i8*
  // CHECK: store i8* [[REMOTE_ELT1_PTR]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[OF:%.+]] = mul nuw i[[SZ]] [[NUM_TEAMS]], 4
  // CHECK: [[POS1:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD]], [[OF]]
  // CHECK: [[POS2:%.+]] = sub nuw i[[SZ]] [[POS1]], 1
  // CHECK: [[POS3:%.+]] = udiv i[[SZ]] [[POS2]], 128
  // CHECK: [[POS4:%.+]] = add nuw i[[SZ]] [[POS3]], 1
  // CHECK: [[SCRATCHPAD_NEXT:%.+]] = mul nuw i[[SZ]] [[POS4]], 128
  //
  // CHECK: [[P:%.+]] = mul nuw i[[SZ]] 2, [[TEAM]]
  // CHECK: [[SCRATCHPAD_ELT_PTR64:%.+]] = add nuw i[[SZ]] [[SCRATCHPAD_NEXT]], [[P]]
  // CHECK: [[SCRATCHPAD_ELT_PTR_VOID:%.+]] = inttoptr i[[SZ]] [[SCRATCHPAD_ELT_PTR64]] to i8*

  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[SCRATCHPAD_ELT_PTR:%.+]] = bitcast i8* [[SCRATCHPAD_ELT_PTR_VOID]] to i16*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i16, i16* [[SCRATCHPAD_ELT_PTR]], align
  // CHECK: store i16 [[REMOTE_ELT_VAL]], i16* [[REMOTE_ELT2]], align
  // CHECK: [[REMOTE_ELT_PTR:%.+]] = bitcast i16* [[REMOTE_ELT2]] to i8*
  // CHECK: store i8* [[REMOTE_ELT_PTR]], i8** [[REMOTE_ELT_REF]], align
  //
  // CHECK: [[REDUCE:%.+]] = icmp ne i32 [[SHOULD_REDUCE]], 0
  // CHECK: br i1 [[REDUCE]], label {{%?}}[[DO_REDUCE:.+]], label {{%?}}[[REDUCE_ELSE:.+]]
  //
  // CHECK: [[DO_REDUCE]]
  // CHECK: [[RED_LIST1_VOID:%.+]] = bitcast [[RLT]]* [[RED_LIST]] to i8*
  // CHECK: [[RED_LIST2_VOID:%.+]] = bitcast [[RLT]]* [[REMOTE_RED_LIST]] to i8*
  // CHECK: call void [[REDUCTION_FUNC]](i8* [[RED_LIST1_VOID]], i8* [[RED_LIST2_VOID]])
  // CHECK: br label {{%?}}[[REDUCE_CONT:.+]]
  //
  // Copy element from remote reduce list
  // CHECK: [[REDUCE_ELSE]]
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 0
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i32*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i32*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i32, i32* [[REMOTE_ELT]], align
  // CHECK: store i32 [[REMOTE_ELT_VAL]], i32* [[ELT]], align
  //
  // CHECK: [[REMOTE_ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[REMOTE_RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[REMOTE_ELT_VOID:%.+]] = load i8*, i8** [[REMOTE_ELT_REF]],
  // CHECK: [[ELT_REF:%.+]] = getelementptr inbounds [[RLT]], [[RLT]]* [[RED_LIST:%.+]], i[[SZ]] 0, i[[SZ]] 1
  // CHECK: [[ELT_VOID:%.+]] = load i8*, i8** [[ELT_REF]],
  // CHECK: [[REMOTE_ELT:%.+]] = bitcast i8* [[REMOTE_ELT_VOID]] to i16*
  // CHECK: [[ELT:%.+]] = bitcast i8* [[ELT_VOID]] to i16*
  // CHECK: [[REMOTE_ELT_VAL:%.+]] = load i16, i16* [[REMOTE_ELT]], align
  // CHECK: store i16 [[REMOTE_ELT_VAL]], i16* [[ELT]], align
  // CHECK: br label {{%?}}[[REDUCE_CONT]]
  //
  // CHECK: [[REDUCE_CONT]]
  // CHECK: ret


#endif
